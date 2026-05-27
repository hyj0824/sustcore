/**
 * @file wait.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief thread waiting reasons
 * @version alpha-1.0.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/int.h>
#include <env.h>
#include <logger.h>
#include <syscall/syscall.h>
#include <task/scheduler.h>
#include <task/wait.h>

#include <cassert>
#include <coroutine>
#include <functional>

namespace task::wait {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static WaitReasonManager inst_wait_reason_manager;
    static bool inst_wait_reason_manager_initialized = false;

    namespace {
        /**
         * @brief 唤醒一个不依赖 syscall 协程句柄的普通等待线程.
         *
         * @param tcb 被唤醒线程.
         * @return Result<void> 成功返回空结果.
         */
        [[nodiscard]]
        Result<void> wake_regular_waiter(TCB *tcb) noexcept {
            if (tcb == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            if (!schd::Scheduler::inst().wakeup_waiting(tcb)) {
                unexpect_return(ErrCode::FAILURE);
            }
            void_return();
        }

        /**
         * @brief 清理线程在等待队列中的元数据.
         *
         * @param tcb 目标线程.
         */
        void clear_queue_metadata(TCB *tcb) noexcept {
            assert(tcb != nullptr);
            tcb->wait_reason    = 0;
            tcb->wait_predicate = {};
            tcb->wait_head.clear();
        }

        /**
         * @brief 清理线程等待状态, 并在需要时丢弃 syscall 协程句柄.
         *
         * @param tcb 目标线程.
         */
        void clear_wait_metadata(TCB *tcb) noexcept {
            assert(tcb != nullptr);
            clear_queue_metadata(tcb);
            tcb->syscall_info.handle = nullptr;
        }

        /**
         * @brief 判断线程是否带有待恢复的 syscall 协程句柄.
         *
         * @param tcb 待检查线程.
         * @return true 存在挂起的 syscall 协程.
         * @return false 不存在挂起的 syscall 协程.
         */
        [[nodiscard]]
        bool has_syscall_waiter(TCB *tcb) noexcept {
            return tcb != nullptr && tcb->syscall_info.handle != nullptr;
        }

        /**
         * @brief 按线程类型执行实际唤醒动作.
         *
         * @param tcb 被唤醒线程.
         * @return Result<void> 成功返回空结果.
         */
        [[nodiscard]]
        Result<void> dispatch_waiter(TCB *tcb) noexcept {
            if (tcb == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            return wake_regular_waiter(tcb);
        }

        /**
         * @brief 运行线程私有的唤醒谓词.
         *
         * @param tcb 待检测线程.
         * @return true 谓词允许唤醒.
         * @return false 谓词拒绝唤醒.
         */
        [[nodiscard]]
        bool run_wait_predicate(TCB *tcb) noexcept {
            if (tcb == nullptr) {
                return false;
            }
            if (!tcb->wait_predicate) {
                return true;
            }
            return tcb->wait_predicate(tcb);
        }
    }  // namespace

    CommonAwaiter::CommonAwaiter(WaitReasonId reason, WaitPredicate predicate,
                                 WaitReadyPredicate ready_predicate) noexcept
        : _reason(reason), _predicate(std::move(predicate)),
          _ready_predicate(std::move(ready_predicate)), _result{} {}

    bool CommonAwaiter::await_ready() const noexcept {
        return _ready_predicate && _ready_predicate();
    }

    bool CommonAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
        auto *context = syscall::active_context();
        if (context == nullptr || context->tcb == nullptr) {
            _result = std::unexpected(ErrCode::INVALID_PARAM);
            return false;
        }

        auto *tcb = context->tcb;
        if (tcb->schd_class == schd::ClassType::IDLE) {
            _result = std::unexpected(ErrCode::INVALID_PARAM);
            return false;
        }

        InterruptGuard guard;
        guard.enter();
        if (_ready_predicate && _ready_predicate()) {
            return false;
        }

        tcb->syscall_info.handle = handle;
        loggers::TASK::DEBUG("线程进入等待: pid=%lu tid=%lu sysno=0x%lx reason=%lu",
                            tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid,
                            tcb->syscall_info.syscall_number, _reason);
        auto enqueue_res =
            WaitReasonManager::inst().enqueue(_reason, tcb, std::move(_predicate));
        if (!enqueue_res.has_value()) {
            tcb->syscall_info.handle = nullptr;
            _result                  = std::unexpected(enqueue_res.error());
            return false;
        }

        tcb->basic_entity.template flags_set<schd::SchedMeta::FLAGS_NEED_RESCHED>();
        _result = {};
        return true;
    }

    Result<void> CommonAwaiter::await_resume() const noexcept {
        return _result;
    }

    bool resume_deferred_syscall(TCB *tcb) noexcept {
        if (!has_syscall_waiter(tcb)) {
            return true;
        }
        if (tcb->syscall_info.completed()) {
            tcb->syscall_info.handle = nullptr;
            return true;
        }
        if (tcb->task == nullptr) {
            loggers::TASK::ERROR("恢复 syscall 协程失败: 线程缺少所属进程");
            return false;
        }

        task::SyscallContext context{
            .tcb          = tcb,
            .pcb          = tcb->task,
            .tmm          = tcb->task->tmm.get(),
            .trap_context = tcb->syscall_info.context.trap_context,
        };
        tcb->syscall_info.context = context;

        auto *previous_context = syscall::active_context();
        syscall::set_active_context(&tcb->syscall_info.context);
        loggers::TASK::DEBUG(
            "调度前恢复 syscall 协程: pid=%lu tid=%lu sysno=0x%lx",
            tcb->task->pid, tcb->tid, tcb->syscall_info.syscall_number);
        tcb->syscall_info.handle.resume();
        syscall::set_active_context(previous_context);

        if (tcb->syscall_info.completed()) {
            loggers::TASK::DEBUG(
                "调度前恢复 syscall 协程完成: pid=%lu tid=%lu sysno=0x%lx",
                tcb->task->pid, tcb->tid, tcb->syscall_info.syscall_number);
            tcb->syscall_info.handle = nullptr;
            return true;
        }
        if (tcb->basic_entity.state == ThreadState::WAITING) {
            loggers::TASK::DEBUG(
                "调度前恢复 syscall 协程后再次等待: pid=%lu tid=%lu sysno=0x%lx",
                tcb->task->pid, tcb->tid, tcb->syscall_info.syscall_number);
            return false;
        }

        loggers::TASK::ERROR(
            "调度前恢复 syscall 协程后状态异常: pid=%lu tid=%lu sysno=0x%lx state=%d",
            tcb->task->pid, tcb->tid, tcb->syscall_info.syscall_number,
            static_cast<int>(tcb->basic_entity.state));
        return false;
    }

    WaitReasonManager &WaitReasonManager::inst() {
        if (!initialized()) {
            panic("WaitReasonManager 未初始化!");
        }
        return inst_wait_reason_manager;
    }

    void WaitReasonManager::init() {
        new (&inst_wait_reason_manager) WaitReasonManager();
        inst_wait_reason_manager_initialized = true;
    }

    bool WaitReasonManager::initialized() {
        return inst_wait_reason_manager_initialized;
    }

    WaitReasonId WaitReasonManager::alloc_reason() {
        return _next_reason++;
    }

    Result<WaitQueue *> WaitReasonManager::queue_for_wait(WaitReasonId id) {
        if (id == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto qres = _queues.at_nt(id);
        if (qres.has_value()) {
            return qres.value().get();
        }

        auto *queue = new WaitQueue(id);
        if (queue == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        _queues[id] = queue;
        return queue;
    }

    Result<WaitQueue *> WaitReasonManager::queue_if_exists(WaitReasonId id) {
        if (id == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        return _queues.at_nt(id).transform(unwrap_ref<WaitQueue *>())
            .value_or(nullptr);
    }

    Result<void> WaitReasonManager::enqueue(WaitReasonId id, TCB *tcb) {
        return enqueue(id, tcb, {});
    }

    Result<void> WaitReasonManager::enqueue(WaitReasonId id, TCB *tcb,
                                            WaitPredicate predicate) {
        if (tcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        auto qres = queue_for_wait(id);
        propagate(qres);

        tcb->wait_reason        = id;
        tcb->wait_predicate     = std::move(predicate);
        tcb->basic_entity.state = ThreadState::WAITING;
        qres.value()->threads.push_back(*tcb);
        loggers::TASK::DEBUG("等待队列入队: pid=%lu tid=%lu reason=%lu",
                             tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid,
                             id);
        void_return();
    }

    Result<TCB *> WaitReasonManager::peek_one(WaitReasonId id) {
        auto qres = queue_if_exists(id);
        propagate(qres);

        WaitQueue *queue = qres.value();
        if (queue == nullptr || queue->threads.empty()) {
            return static_cast<TCB *>(nullptr);
        }
        return &queue->threads.front();
    }

    Result<TCB *> WaitReasonManager::pop_one(WaitReasonId id) {
        auto qres = queue_if_exists(id);
        propagate(qres);

        WaitQueue *queue = qres.value();
        if (queue == nullptr || queue->threads.empty()) {
            return static_cast<TCB *>(nullptr);
        }

        TCB *tcb = &queue->threads.front();
        queue->threads.pop_front();
        clear_wait_metadata(tcb);
        return tcb;
    }

    Result<void> WaitReasonManager::remove(TCB *tcb) {
        if (tcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (tcb->wait_reason == 0) {
            void_return();
        }

        auto qres = queue_if_exists(tcb->wait_reason);
        propagate(qres);
        WaitQueue *queue = qres.value();
        if (queue != nullptr) {
            queue->threads.remove(*tcb);
        }
        clear_wait_metadata(tcb);
        void_return();
    }

    Result<size_t> WaitReasonManager::wake_one(WaitReasonId id) {
        auto qres = queue_if_exists(id);
        propagate(qres);

        WaitQueue *queue = qres.value();
        if (queue == nullptr || queue->threads.empty()) {
            return size_t(0);
        }

        TCB *first_rejected = nullptr;
        while (!queue->threads.empty()) {
            TCB *tcb = &queue->threads.front();
            queue->threads.pop_front();

            if (tcb == first_rejected) {
                queue->threads.push_back(*tcb);
                return size_t(0);
            }

            if (run_wait_predicate(tcb)) {
                if (has_syscall_waiter(tcb)) {
                    clear_queue_metadata(tcb);
                } else {
                    clear_wait_metadata(tcb);
                }
                auto resume_res = dispatch_waiter(tcb);
                propagate(resume_res);
                return size_t(1);
            }

            if (first_rejected == nullptr) {
                first_rejected = tcb;
            }
            queue->threads.push_back(*tcb);
        }

        return size_t(0);
    }

    Result<size_t> WaitReasonManager::wake_all(WaitReasonId id) {
        size_t count = 0;
        auto qres    = queue_if_exists(id);
        propagate(qres);

        WaitQueue *queue = qres.value();
        if (queue == nullptr || queue->threads.empty()) {
            return count;
        }

        size_t scan_count = queue->threads.size();
        for (size_t i = 0; i < scan_count; ++i) {
            TCB *tcb = &queue->threads.front();
            queue->threads.pop_front();

            if (!run_wait_predicate(tcb)) {
                queue->threads.push_back(*tcb);
                continue;
            }

            if (has_syscall_waiter(tcb)) {
                clear_queue_metadata(tcb);
            } else {
                clear_wait_metadata(tcb);
            }
            auto resume_res = dispatch_waiter(tcb);
            propagate(resume_res);
            ++count;
        }
        return count;
    }

    bool WaitReasonManager::has_waiting(WaitReasonId id) {
        auto qres = queue_if_exists(id);
        if (!qres.has_value()) {
            return false;
        }
        WaitQueue *queue = qres.value();
        return queue != nullptr && !queue->threads.empty();
    }

    WaitReasonId alloc_reason() {
        return WaitReasonManager::inst().alloc_reason();
    }

    Result<void> deprecated_wait_current(WaitReasonId id) {
        return schd::Scheduler::inst().block_current(id);
    }

    Result<void> deprecated_wait_current(WaitReasonId id,
                                         WaitPredicate predicate) {
        return schd::Scheduler::inst().block_current(id, std::move(predicate));
    }

    Result<TCB *> peek_one(WaitReasonId id) {
        return WaitReasonManager::inst().peek_one(id);
    }

    Result<size_t> wake_one(WaitReasonId id) {
        return WaitReasonManager::inst().wake_one(id);
    }

    Result<size_t> wake_all(WaitReasonId id) {
        return WaitReasonManager::inst().wake_all(id);
    }

    bool has_waiting(WaitReasonId id) {
        return WaitReasonManager::inst().has_waiting(id);
    }
}  // namespace task::wait
