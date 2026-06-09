/**
 * @file request.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 块设备请求层
 * @version alpha-1.0.0
 * @date 2026-06-09
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <spinlock.h>
#include <sus/nonnull.h>
#include <sus/ringbuf.h>
#include <sustcore/errcode.h>
#include <task/wait.h>

#include <cstddef>
#include <functional>

using lba_t = size_t;

namespace blk {
    enum class BlockOp {
        READ,
        WRITE,
        FLUSH,
    };

    enum class BlockReqStatus {
        PENDING,
        SUBMITTED,
        COMPLETED,
        FAILED,
        CANCELED,
    };

    struct BlockRequest {
        BlockOp op = BlockOp::READ;
        size_t devno = 0;
        lba_t lba = 0;
        size_t block_count = 0;
        void *buffer = nullptr;
        BlockReqStatus status = BlockReqStatus::PENDING;
        Result<size_t> result = std::unexpected(ErrCode::FUTURE_PENDING);
        PromiseResult<size_t> promise{};
        std::function<void(BlockRequest &)> on_complete{};
    };

    class BlockRequestQueue {
    private:
        size_t _devno = 0;
        SpinLocker _lock{};
        util::RingBuffer<BlockRequest *> _ring{};
        size_t _wait_reason = 0;
        bool _accepting = true;
        bool _stopped = false;

    public:
        explicit BlockRequestQueue(size_t devno, size_t depth = 128);

        BlockRequestQueue(const BlockRequestQueue &) = delete;
        BlockRequestQueue &operator=(const BlockRequestQueue &) = delete;
        BlockRequestQueue(BlockRequestQueue &&) = delete;
        BlockRequestQueue &operator=(BlockRequestQueue &&) = delete;
        ~BlockRequestQueue() = default;

        [[nodiscard]]
        size_t devno() const noexcept {
            return _devno;
        }

        [[nodiscard]]
        bool stopped() const noexcept;

        [[nodiscard]]
        bool accepting() const noexcept;

        [[nodiscard]]
        Result<void> submit(util::nonnull<BlockRequest *> req);

        [[nodiscard]]
        Result<BlockRequest *> try_dequeue();

        [[nodiscard]]
        Result<BlockRequest *> wait_and_dequeue();

        [[nodiscard]]
        Result<void> complete(util::nonnull<BlockRequest *> req,
                              Result<size_t> result);

        [[nodiscard]]
        Result<void> stop_accepting();

        [[nodiscard]]
        Result<void> drain_and_stop(ErrCode error);

        [[nodiscard]]
        size_t wait_reason() const noexcept {
            return _wait_reason;
        }
    };

    class BlockRequestLayer {
    private:
        BlockRequestLayer() = default;
        ~BlockRequestLayer() = default;

        static BlockRequestLayer _INSTANCE;
        static bool _initialized;

        [[nodiscard]]
        FutureResult<size_t> make_error_future(ErrCode error) const;

        [[nodiscard]]
        FutureResult<size_t> submit_async(BlockOp op, size_t devno,
                                                        lba_t lba, void *buffer,
                                                        size_t cnt) const;

    public:
        static void init();
        static bool initialized();
        static BlockRequestLayer &inst();

        [[nodiscard]]
        FutureResult<size_t> submit_read_async(size_t devno,
                                                             lba_t lba,
                                                             void *buf,
                                                             size_t cnt) const;

        [[nodiscard]]
        FutureResult<size_t> submit_write_async(size_t devno,
                                                              lba_t lba,
                                                              const void *buf,
                                                              size_t cnt) const;

        [[nodiscard]]
        FutureResult<size_t> submit_flush_async(size_t devno) const;
    };
}  // namespace blk
