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
    /**
     * @brief 块请求的操作类型.
     */
    enum class BlockOp {
        READ,
        WRITE,
        FLUSH,
    };

    /**
     * @brief 块请求状态机.
     *
     * - `PENDING`: 请求对象刚创建, 尚未入队.
     * - `SUBMITTED`: 请求已成功入队, 等待 worker 消费.
     * - `PROCESSING`: worker 已取出请求并将其交由设备驱动处理.
     * - `COMPLETED`: 请求成功完成, future 已决议为成功结果.
     * - `FAILED`: 请求处理失败, future 已决议为错误结果.
     * - `CANCELED`: 请求在进入 PROCESSING 之前被终止.
     */
    enum class BlockReqStatus {
        PENDING,
        SUBMITTED,
        PROCESSING,
        COMPLETED,
        FAILED,
        CANCELED,
    };

    /**
     * @brief 单个块 I/O 请求描述符.
     *
     * 请求对象由 `BlockRequestLayer` 创建并提交到设备专属队列中.
     * 其生命周期由提交路径与完成路径共同管理: 创建时处于 `PENDING`,
     * 完成或取消后由 `on_complete` 统一回收.
     */
    struct BlockRequest {
        BlockOp op            = BlockOp::READ;
        size_t devno          = 0;
        lba_t lba             = 0;
        size_t block_count    = 0;
        void *buffer          = nullptr;
        BlockReqStatus status = BlockReqStatus::PENDING;
        Result<size_t> result = std::unexpected(ErrCode::FUTURE_PENDING);
        PromiseResult<size_t> promise{};
        std::function<void(BlockRequest &)> on_complete{};
    };

    /**
     * @brief 设备专属块请求队列.
     *
     * 每个块设备对应一个 `BlockRequestQueue`. 生产者可并发提交请求, 消费者固定为
     * 设备 worker 线程. 队列负责维护请求状态机中的入队、出队、取消与完成阶段,
     * 并在终态决议时写入 future.
     */
    class BlockRequestQueue {
    private:
        size_t _devno = 0;
        SpinLocker _lock{};
        util::RingBuffer<BlockRequest *> _ring{};
        wait::wd_t _wait_wd = 0;
        bool _accepting     = true;
        bool _stopped       = false;

    public:
        explicit BlockRequestQueue(size_t devno, size_t depth = 128);

        BlockRequestQueue(const BlockRequestQueue &)            = delete;
        BlockRequestQueue &operator=(const BlockRequestQueue &) = delete;
        BlockRequestQueue(BlockRequestQueue &&)                 = delete;
        BlockRequestQueue &operator=(BlockRequestQueue &&)      = delete;
        ~BlockRequestQueue()                                    = default;

        [[nodiscard]]
        size_t devno() const noexcept {
            return _devno;
        }

        [[nodiscard]]
        bool stopped() noexcept;

        [[nodiscard]]
        bool accepting() noexcept;

        [[nodiscard]]
        Result<void> submit(util::nonnull<BlockRequest *> req);

        /**
         * @brief 非阻塞尝试取出一个请求.
         *
         * 成功时返回队首请求指针并将其移出 ring.
         * 若队列为空则返回 `ENTRY_NOT_FOUND`, 若队列已停止则返回
         * `FUTURE_CANCLED`.
         */
        [[nodiscard]]
        Result<BlockRequest *> try_dequeue();

        /**
         * @brief 阻塞等待直到有请求可消费或队列停止.
         */
        [[nodiscard]]
        Result<BlockRequest *> wait_and_dequeue();

        /**
         * @brief 将请求从 SUBMITTED 推进到 PROCESSING.
         *
         * 该接口通常由设备 worker 在调用 `process_request()` 之前执行.
         */
        [[nodiscard]]
        Result<void> mark_processing(util::nonnull<BlockRequest *> req);

        /**
         * @brief 完成一个已进入 PROCESSING 的请求.
         *
         * 该接口会根据结果将请求推进到 `COMPLETED/FAILED`, 写入
         * `promise/future`, 并在最后触发 `on_complete`.
         */
        [[nodiscard]]
        Result<void> complete(util::nonnull<BlockRequest *> req,
                              Result<size_t> result);

        /**
         * @brief 取消一个尚未进入 PROCESSING 的请求.
         *
         * 典型使用场景是 stop/drain 阶段对队列中未处理请求进行终止.
         */
        [[nodiscard]]
        Result<void> cancel(util::nonnull<BlockRequest *> req, ErrCode error);

        /**
         * @brief 停止接受新请求.
         */
        [[nodiscard]]
        Result<void> stop_accepting();

        /**
         * @brief 终止队列并取消所有尚未处理的请求.
         *
         * 该接口会先拒绝新请求, 再取消 ring 中仍停留在
         * `PENDING/SUBMITTED` 的请求, 最后唤醒等待线程并标记队列停止.
         */
        [[nodiscard]]
        Result<void> drain_and_stop(ErrCode error);

        [[nodiscard]]
        wait::wd_t wait_wd() const noexcept {
            return _wait_wd;
        }
    };

    /**
     * @brief 设备私有块请求提交器.
     *
     * 每个块设备对应一份 `BlockRequestLayer`, 并由对应 `BufferCache`
     * 独占持有. 该对象绑定单个 `BlockRequestQueue`, 负责把上层块读写请求
     * 转换为 `BlockRequest` 并入队.
     *
     * 与早期全局单例模型不同, 当前 `BlockRequestLayer` 不再通过 `devno`
     * 动态查找队列, 而是在构造时固定绑定本设备的 queue.
     */
    class BlockRequestLayer {
    private:
        size_t _devno             = 0;
        BlockRequestQueue *_queue = nullptr;

        [[nodiscard]]
        FutureResult<size_t> make_error_future(ErrCode error) const;

        [[nodiscard]]
        FutureResult<size_t> submit_async(BlockOp op, lba_t lba, void *buffer,
                                          size_t cnt) const;

    public:
        explicit BlockRequestLayer(util::nonnull<BlockRequestQueue *> queue);

        BlockRequestLayer(const BlockRequestLayer &)            = delete;
        BlockRequestLayer &operator=(const BlockRequestLayer &) = delete;
        BlockRequestLayer(BlockRequestLayer &&)                 = delete;
        BlockRequestLayer &operator=(BlockRequestLayer &&)      = delete;
        ~BlockRequestLayer()                                    = default;

        [[nodiscard]]
        Result<void> mark_request_processing(
            util::nonnull<BlockRequest *> req) const;

        /**
         * @brief 将请求标记为成功或失败完成.
         *
         * 这是对底层 queue 完成原语的设备私有转发接口.
         */
        [[nodiscard]]
        Result<void> complete_request(util::nonnull<BlockRequest *> req,
                                      Result<size_t> result) const;

        /**
         * @brief 提交块读请求.
         *
         * 返回值始终是 `Future<Result<size_t>>`. 若请求在真正入队前失败,
         * 该 future 会被立即决议为错误结果.
         */
        [[nodiscard]]
        FutureResult<size_t> submit_read_async(lba_t lba, void *buf,
                                               size_t cnt) const;

        /**
         * @brief 提交块写请求.
         */
        [[nodiscard]]
        FutureResult<size_t> submit_write_async(lba_t lba, const void *buf,
                                                size_t cnt) const;

        /**
         * @brief 提交设备 flush 请求.
         */
        [[nodiscard]]
        FutureResult<size_t> submit_flush_async() const;
    };
}  // namespace blk
