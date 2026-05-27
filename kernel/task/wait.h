/**
 * @file wait.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief thread waiting reasons
 * @version alpha-1.0.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sus/list.h>
#include <sus/map.h>
#include <sus/coroutine.h>
#include <sustcore/errcode.h>
#include <task/task_struct.h>
#include <unordered_map>
#include <utility>

namespace task::wait {
    /**
     * @brief syscall 协程通用等待点.
     *
     * 该 awaiter 会在挂起时自动把所属线程标记为 WAITING 并登记协程句柄,
     * 条件满足时由等待系统唤醒所属线程, 再由 scheduler 在切换到该线程前恢复执行.
     */
    class CommonAwaiter {
    private:
        WaitReasonId _reason = 0;
        WaitPredicate _predicate;
        WaitReadyPredicate _ready_predicate;
        Result<void> _result{};

    public:
        /**
         * @brief 构造一个通用等待点.
         *
         * @param reason 等待原因.
         * @param predicate 唤醒前检查的线程谓词.
         * @param ready_predicate 挂起前的就绪判定.
         */
        explicit CommonAwaiter(WaitReasonId reason,
                               WaitPredicate predicate = {},
                               WaitReadyPredicate ready_predicate = {}) noexcept;

        /**
         * @brief 判断等待条件在挂起前是否已经满足.
         *
         * @return true 无需挂起.
         * @return false 需要挂起.
         */
        [[nodiscard]]
        bool await_ready() const noexcept;

        /**
         * @brief 挂起当前 syscall 协程并登记等待元数据.
         *
         * @param handle 当前协程句柄.
         * @return true 挂起协程.
         * @return false 不挂起, 立即继续执行.
         */
        bool await_suspend(std::coroutine_handle<> handle) noexcept;

        /**
         * @brief 恢复协程时返回等待结果.
         *
         * @return Result<void> 等待结果.
         */
        [[nodiscard]]
        Result<void> await_resume() const noexcept;
    };

    // 等待队列
    struct WaitQueue {
        // 等待队列对应的等待id
        WaitReasonId id;
        util::IntrusiveList<TCB, &TCB::wait_head> threads;

        // 从wait id中创建一个等待队列
        explicit WaitQueue(WaitReasonId id) : id(id), threads() {}
    };

    // 等待原因管理器, 负责管理所有的等待队列
    class WaitReasonManager {
    private:
        // 编号分配
        WaitReasonId _next_reason = 1;
        // wait_id -> wait_queue
        std::unordered_map<WaitReasonId, WaitQueue *> _queues;

        // 获取等待队列, 如果不存在则创建一个新的
        Result<WaitQueue *> queue_for_wait(WaitReasonId id);
        // 获取等待队列, 如果不存在则返回错误
        Result<WaitQueue *> queue_if_exists(WaitReasonId id);
    public:
        // 分配一个 wait reason 号
        WaitReasonId alloc_reason();
        // 将当前线程加入等待队列
        Result<void> enqueue(WaitReasonId id, TCB *tcb);
        Result<void> enqueue(WaitReasonId id, TCB *tcb,
                             WaitPredicate predicate);
        Result<TCB *> peek_one(WaitReasonId id);
        // 从等待队列中弹出一个线程
        Result<TCB *> pop_one(WaitReasonId id);
        Result<void> remove(TCB *tcb);
        // 从等待队列中唤醒一个线程, 返回被唤醒线程的数量(0或1)
        Result<size_t> wake_one(WaitReasonId id);
        // 从等待队列中唤醒所有线程, 返回被唤醒线程的数量
        Result<size_t> wake_all(WaitReasonId id);
        // 判断是否有线程在等待队列中
        bool has_waiting(WaitReasonId id);

        // 初始化等待原因管理器的单例实例
        static void init();
        static bool initialized();
        // 获取等待原因管理器的单例实例
        static WaitReasonManager &inst();
    };

    WaitReasonId alloc_reason();
    [[deprecated("use co_await wait_current(...) in syscall coroutine paths")]]
    Result<void> deprecated_wait_current(WaitReasonId id);
    [[deprecated("use co_await wait_current(...) in syscall coroutine paths")]]
    Result<void> deprecated_wait_current(WaitReasonId id,
                                         WaitPredicate predicate);
    /**
     * @brief 在目标线程真正被调度运行前恢复其挂起的 syscall 协程.
     *
     * @param tcb 即将进入运行态的线程.
     * @return true 该线程可以继续参与本轮调度.
     * @return false 该线程恢复后重新进入等待或恢复失败, 需要跳过.
     */
    [[nodiscard]]
    bool resume_deferred_syscall(TCB *tcb) noexcept;
    Result<TCB *> peek_one(WaitReasonId id);
    Result<size_t> wake_one(WaitReasonId id);
    Result<size_t> wake_all(WaitReasonId id);
    bool has_waiting(WaitReasonId id);
}  // namespace task::wait
