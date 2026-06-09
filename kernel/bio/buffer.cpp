/**
 * @file buffer.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 块缓存实现
 * @version alpha-1.0.0
 * @date 2026-06-09
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <bio/buffer.h>
#include <bio/request.h>

#include <cstring>

namespace blk {
    BufferCache::BufferCache(size_t devno, size_t blksz)
        : _devno(devno), _blksz(blksz) {
        assert(_blksz != 0);
    }

    BufferCache::~BufferCache() {
        for (size_t i = 0; i < MAX_CACHE_SIZE; ++i) {
            if (_buffers[i].get() == nullptr) {
                continue;
            }
            delete[] _buffers[i]->data;
            delete _buffers[i].get();
            _buffers[i] = util::owner<Buffer *>(nullptr);
        }
        _mapping.clear();
    }

    FutureResult<void> BufferCache::make_void_future(
        Result<void> result) {
        PromiseResult<void> promise;
        auto future = promise.future();
        auto set_res = promise.set_value(result);
        assert(set_res.has_value());
        return future;
    }

    FutureResult<BufferHandler> BufferCache::make_handler_future(
        Result<BufferHandler> result) {
        PromiseResult<BufferHandler> promise;
        auto future = promise.future();
        auto set_res = promise.set_value(std::move(result));
        assert(set_res.has_value());
        return future;
    }

    Result<void> BufferCache::clear_slot(size_t idx) {
        if (idx >= MAX_CACHE_SIZE) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        Buffer *buffer = _buffers[idx].get();
        if (buffer == nullptr) {
            void_return();
        }
        if (buffer->refcnt != 0 || buffer->inflight) {
            unexpect_return(ErrCode::BUSY);
        }
        auto erase_it = _mapping.find(buffer->blkno);
        if (erase_it != _mapping.end() && erase_it->second == idx) {
            _mapping.erase(erase_it);
        }
        delete[] buffer->data;
        delete buffer;
        _buffers[idx] = util::owner<Buffer *>(nullptr);
        void_return();
    }

    Result<size_t> BufferCache::find_free() {
        for (size_t i = 0; i < MAX_CACHE_SIZE; ++i) {
            if (_buffers[i].get() == nullptr) {
                return i;
            }
        }

        for (size_t i = 0; i < MAX_CACHE_SIZE; ++i) {
            Buffer *buffer = _buffers[i].get();
            if (buffer == nullptr || buffer->refcnt != 0 || buffer->inflight) {
                continue;
            }
            auto clear_res = clear_slot(i);
            propagate(clear_res);
            return i;
        }

        unexpect_return(ErrCode::BUSY);
    }

    Result<size_t> BufferCache::find_buffer(lba_t blkno) {
        auto map_res = _mapping.at_nt(blkno);
        if (!map_res.has_value()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        return map_res.value().get();
    }

    Result<size_t> BufferCache::ensure_buffer(lba_t blkno) {
        auto found_res = find_buffer(blkno);
        if (found_res.has_value()) {
            return found_res.value();
        }
        if (found_res.error() != ErrCode::ENTRY_NOT_FOUND) {
            propagate_return(found_res);
        }

        auto free_res = find_free();
        propagate(free_res);

        auto *buffer = new Buffer{
            .blkno = blkno,
            .data = new char[_blksz],
            .dirty = false,
            .valid = false,
            .inflight = false,
            .refcnt = 0,
        };
        if (buffer == nullptr || buffer->data == nullptr) {
            delete[] (buffer == nullptr ? nullptr : buffer->data);
            delete buffer;
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        size_t idx    = free_res.value();
        _buffers[idx] = util::owner<Buffer *>(buffer);
        _mapping.insert_or_assign(blkno, idx);
        return idx;
    }

    FutureResult<void> BufferCache::sync(BufferHandler &handler) {
        if (handler._cache != this || handler._buf == nullptr) {
            return make_void_future(std::unexpected(ErrCode::INVALID_PARAM));
        }
        auto &buffer = *handler._buf;
        if (!buffer.valid || !buffer.dirty) {
            return make_void_future({});
        }
        if (buffer.inflight) {
            return make_void_future(std::unexpected(ErrCode::BUSY));
        }

        buffer.inflight = true;
        auto lower_future = BlockRequestLayer::inst().submit_write_async(
            _devno, buffer.blkno, buffer.data, 1);
        auto lower_res = task::wait::kthread_wait_for(lower_future);
        buffer.inflight = false;
        if (!lower_res.has_value()) {
            return make_void_future(std::unexpected(lower_res.error()));
        }
        if (lower_res.value() != 1) {
            return make_void_future(std::unexpected(ErrCode::IO_ERROR));
        }

        buffer.dirty = false;

        auto flush_future = BlockRequestLayer::inst().submit_flush_async(_devno);
        auto flush_res = task::wait::kthread_wait_for(flush_future);
        if (!flush_res.has_value()) {
            return make_void_future(std::unexpected(flush_res.error()));
        }

        return make_void_future({});
    }

    FutureResult<void> BufferCache::sync_all() {
        for (size_t i = 0; i < MAX_CACHE_SIZE; ++i) {
            Buffer *buffer = _buffers[i].get();
            if (buffer == nullptr) {
                continue;
            }
            BufferHandler handler(util::nnullforce(buffer), *this);
            auto sync_future = sync(handler);
            auto sync_res = task::wait::kthread_wait_for(sync_future);
            if (!sync_res.has_value()) {
                return make_void_future(std::unexpected(sync_res.error()));
            }
        }
        return make_void_future({});
    }

    FutureResult<void> BufferCache::tidy() {
        auto sync_future = sync_all();
        auto sync_res = task::wait::kthread_wait_for(sync_future);
        if (!sync_res.has_value()) {
            return make_void_future(std::unexpected(sync_res.error()));
        }

        for (size_t i = 0; i < MAX_CACHE_SIZE; ++i) {
            Buffer *buffer = _buffers[i].get();
            if (buffer == nullptr || buffer->refcnt != 0 || buffer->inflight) {
                continue;
            }
            auto clear_res = clear_slot(i);
            if (!clear_res.has_value()) {
                return make_void_future(std::unexpected(clear_res.error()));
            }
        }
        return make_void_future({});
    }

    FutureResult<BufferHandler> BufferCache::get_buffer_async(
        lba_t blkno) {
        auto idx_res = ensure_buffer(blkno);
        if (!idx_res.has_value()) {
            return make_handler_future(std::unexpected(idx_res.error()));
        }

        Buffer *buffer = _buffers[idx_res.value()].get();
        if (buffer == nullptr) {
            return make_handler_future(std::unexpected(ErrCode::UNKNOWN_ERROR));
        }

        if (buffer->valid) {
            return make_handler_future(
                BufferHandler(util::nnullforce(buffer), *this));
        }
        if (buffer->inflight) {
            return make_handler_future(std::unexpected(ErrCode::BUSY));
        }

        buffer->inflight = true;
        auto lower_future = BlockRequestLayer::inst().submit_read_async(
            _devno, blkno, buffer->data, 1);
        auto lower_res = task::wait::kthread_wait_for(lower_future);
        buffer->inflight = false;
        if (!lower_res.has_value()) {
            return make_handler_future(std::unexpected(lower_res.error()));
        }
        if (lower_res.value() != 1) {
            return make_handler_future(std::unexpected(ErrCode::IO_ERROR));
        }

        buffer->valid = true;
        return make_handler_future(BufferHandler(util::nnullforce(buffer), *this));
    }
}  // namespace blk
