/**
 * @file request.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 块设备请求层实现
 * @version alpha-1.0.0
 * @date 2026-06-09
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <bio/blk.h>
#include <bio/request.h>
#include <logger.h>

#include <cassert>
#include <new>

namespace blk {
    BlockRequestQueue::BlockRequestQueue(size_t devno, size_t depth)
        : _devno(devno),
          _ring(depth + 1),
          _wait_wd(wait::alloc_reason()) {}

    bool BlockRequestQueue::stopped() noexcept {
        GuardedLock lock(_lock);
        return _stopped;
    }

    bool BlockRequestQueue::accepting() noexcept {
        GuardedLock lock(_lock);
        return _accepting;
    }

    Result<void> BlockRequestQueue::submit(util::nonnull<BlockRequest *> req) {
        {
            GuardedLock lock(_lock);
            if (!_accepting || _stopped) {
                unexpect_return(ErrCode::BUSY);
            }
            assert(!_ring.full());  // TODO: 队列满时改为正式背压策略
            auto push_res = _ring.push(req.get());
            if (!push_res.has_value()) {
                unexpect_return(ErrCode::BUSY);
            }
            req->status = BlockReqStatus::SUBMITTED;
        }
        auto wake_res = wait::locked_wake_all(_wait_wd, _lock);
        propagate(wake_res);
        void_return();
    }

    Result<BlockRequest *> BlockRequestQueue::try_dequeue() {
        GuardedLock lock(_lock);
        if (_ring.empty()) {
            if (_stopped) {
                unexpect_return(ErrCode::FUTURE_CANCLED);
            }
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        auto *req    = _ring.front();
        auto pop_res = _ring.pop();
        if (!pop_res.has_value()) {
            unexpect_return(ErrCode::FAILURE);
        }
        return req;
    }

    Result<BlockRequest *> BlockRequestQueue::wait_and_dequeue() {
        while (true) {
            auto pop_res = try_dequeue();
            if (pop_res.has_value()) {
                return pop_res.value();
            }
            if (pop_res.error() == ErrCode::FUTURE_CANCLED) {
                propagate_return(pop_res);
            }
            if (pop_res.error() != ErrCode::ENTRY_NOT_FOUND) {
                propagate_return(pop_res);
            }
            auto wait_res = wait::locked_wait_event(
                _wait_wd, _lock,
                [this]() noexcept { return _stopped || !_ring.empty(); });
            propagate(wait_res);
        }
    }

    Result<void> BlockRequestQueue::mark_processing(
        util::nonnull<BlockRequest *> req) {
        if (req->status != BlockReqStatus::SUBMITTED) {
            unexpect_return(ErrCode::BUSY);
        }
        req->status = BlockReqStatus::PROCESSING;
        void_return();
    }

    Result<void> BlockRequestQueue::complete(util::nonnull<BlockRequest *> req,
                                             Result<size_t> result) {
        if (req->status != BlockReqStatus::PROCESSING) {
            unexpect_return(ErrCode::BUSY);
        }
        if (result.has_value()) {
            req->status = BlockReqStatus::COMPLETED;
        } else {
            req->status = BlockReqStatus::FAILED;
        }
        req->result  = result;
        auto set_res = req->promise.set_value(result);
        propagate(set_res);
        if (req->on_complete) {
            req->on_complete(*req);
        }
        void_return();
    }

    Result<void> BlockRequestQueue::cancel(util::nonnull<BlockRequest *> req,
                                           ErrCode error) {
        if (req->status == BlockReqStatus::PROCESSING ||
            req->status == BlockReqStatus::COMPLETED ||
            req->status == BlockReqStatus::FAILED ||
            req->status == BlockReqStatus::CANCELED)
        {
            unexpect_return(ErrCode::BUSY);
        }
        req->status  = BlockReqStatus::CANCELED;
        req->result  = std::unexpected(error);
        auto set_res = req->promise.set_value(std::unexpected(error));
        propagate(set_res);
        if (req->on_complete) {
            req->on_complete(*req);
        }
        void_return();
    }

    Result<void> BlockRequestQueue::stop_accepting() {
        GuardedLock lock(_lock);
        _accepting = false;
        void_return();
    }

    Result<void> BlockRequestQueue::drain_and_stop(ErrCode error) {
        {
            GuardedLock lock(_lock);
            _accepting = false;
        }

        while (true) {
            auto req_res = try_dequeue();
            if (req_res.has_value()) {
                auto cancel_res =
                    cancel(util::nnullforce(req_res.value()), error);
                propagate(cancel_res);
                continue;
            }
            if (req_res.error() == ErrCode::ENTRY_NOT_FOUND) {
                break;
            }
            if (req_res.error() == ErrCode::FUTURE_CANCLED) {
                break;
            }
            propagate_return(req_res);
        }

        {
            GuardedLock lock(_lock);
            _stopped = true;
        }
        auto wake_res = wait::locked_wake_all(_wait_wd, _lock);
        propagate(wake_res);
        void_return();
    }

    BlockRequestLayer::BlockRequestLayer(
        util::nonnull<BlockRequestQueue *> queue)
        : _devno(queue->devno()), _queue(queue.get()) {
        assert(_queue != nullptr);
    }

    Result<void> BlockRequestLayer::mark_request_processing(
        util::nonnull<BlockRequest *> req) const {
        if (_queue == nullptr || req->devno != _devno) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return _queue->mark_processing(req);
    }

    Result<void> BlockRequestLayer::complete_request(
        util::nonnull<BlockRequest *> req, Result<size_t> result) const {
        if (_queue == nullptr || req->devno != _devno) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return _queue->complete(req, std::move(result));
    }

    FutureResult<size_t> BlockRequestLayer::make_error_future(
        ErrCode error) const {
        PromiseResult<size_t> promise;
        auto future  = promise.future();
        auto set_res = promise.set_value(std::unexpected(error));
        assert(set_res.has_value());
        return future;
    }

    FutureResult<size_t> BlockRequestLayer::submit_async(BlockOp op, lba_t lba,
                                                         void *buffer,
                                                         size_t cnt) const {
        if (_queue == nullptr) {
            return make_error_future(ErrCode::NULLPTR);
        }

        auto *req = new BlockRequest{
            .op          = op,
            .devno       = _devno,
            .lba         = lba,
            .block_count = cnt,
            .buffer      = buffer,
        };
        if (req == nullptr) {
            return make_error_future(ErrCode::OUT_OF_MEMORY);
        }

        auto future      = req->promise.future();
        req->on_complete = [](BlockRequest &done_req) { delete &done_req; };

        auto submit_res = _queue->submit(util::nnullforce(req));
        if (!submit_res.has_value()) {
            auto set_res =
                req->promise.set_value(std::unexpected(submit_res.error()));
            assert(set_res.has_value());
            delete req;
        }
        return future;
    }

    FutureResult<size_t> BlockRequestLayer::submit_read_async(
        lba_t lba, void *buf, size_t cnt) const {
        if (buf == nullptr && cnt != 0) {
            return make_error_future(ErrCode::NULLPTR);
        }
        return submit_async(BlockOp::READ, lba, buf, cnt);
    }

    FutureResult<size_t> BlockRequestLayer::submit_write_async(
        lba_t lba, const void *buf, size_t cnt) const {
        if (buf == nullptr && cnt != 0) {
            return make_error_future(ErrCode::NULLPTR);
        }
        return submit_async(BlockOp::WRITE, lba, const_cast<void *>(buf), cnt);
    }

    FutureResult<size_t> BlockRequestLayer::submit_flush_async() const {
        return submit_async(BlockOp::FLUSH, 0, nullptr, 0);
    }
}  // namespace blk
