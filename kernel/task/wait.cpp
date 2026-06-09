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

#include <driver/int/base.h>
#include <bio/buffer.h>
#include <logger.h>
#include <object/endpoint.h>
#include <syscall/syscall.h>
#include <task/scheduler.h>
#include <task/wait.h>

#include <cassert>

namespace task::wait {
    const WaitContext WaitContext::EMPTY{};

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static WaitReasonManager inst_wait_reason_manager;
    static bool inst_wait_reason_manager_initialized = false;

    namespace {
        [[nodiscard]]
        Result<TCB *> current_waiter() noexcept {
            auto *current = schd::Scheduler::inst().current_tcb();
            if (current == nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            return current;
        }

        [[nodiscard]]
        Result<TCB *> check_waiting_thread(bool require_kernel) noexcept {
            auto current_res = current_waiter();
            propagate(current_res);
            auto *current = current_res.value();
            if (current->schd_class == schd::ClassType::IDLE) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            if (require_kernel && !current->is_kernel) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            if (!require_kernel && current->is_kernel) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            return current;
        }

        [[nodiscard]]
        Result<TCB *> check_blockable_thread() noexcept {
            auto current_res = current_waiter();
            propagate(current_res);
            auto *current = current_res.value();
            if (current->schd_class == schd::ClassType::IDLE) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            return current;
        }

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

        void clear_wait_metadata(TCB *tcb) noexcept {
            assert(tcb != nullptr);
            clear_queue_metadata(tcb);
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

    Result<void> wait_event(size_t id,
                            WaitReadyPredicate ready_predicate) noexcept {
        if (id == 0 || !ready_predicate) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (ready_predicate()) {
            void_return();
        }

        auto current_res = check_blockable_thread();
        propagate(current_res);

        while (!ready_predicate()) {
            auto wait_res = schd::Scheduler::inst().block_current(id);
            propagate(wait_res);
        }

        void_return();
    }

    Result<void> future_begin_update() noexcept {
        InterruptGuard guard;
        guard.enter();
        void_return();
    }

    Result<void> future_wait_current(size_t reason) noexcept {
        return schd::Scheduler::inst().block_current(reason);
    }

    Result<void> check_future_wait_thread(bool require_kernel) noexcept {
        auto current_res = check_waiting_thread(require_kernel);
        propagate(current_res);
        void_return();
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

    size_t WaitReasonManager::alloc_reason() {
        return _next_reason++;
    }

    Result<WaitQueue *> WaitReasonManager::queue_for_wait(size_t id) {
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

    Result<WaitQueue *> WaitReasonManager::queue_if_exists(size_t id) {
        if (id == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        return _queues.at_nt(id)
            .transform(unwrap_ref<WaitQueue *>())
            .value_or(nullptr);
    }

    Result<void> WaitReasonManager::enqueue(size_t id, TCB *tcb) {
        return enqueue(id, tcb, {});
    }

    Result<void> WaitReasonManager::enqueue(size_t id, TCB *tcb,
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
                             tcb->task != nullptr ? tcb->task->pid : 0,
                             tcb->tid, id);
        void_return();
    }

    Result<TCB *> WaitReasonManager::peek_one(size_t id) {
        auto qres = queue_if_exists(id);
        propagate(qres);

        WaitQueue *queue = qres.value();
        if (queue == nullptr || queue->threads.empty()) {
            return static_cast<TCB *>(nullptr);
        }
        return &queue->threads.front();
    }

    Result<TCB *> WaitReasonManager::pop_one(size_t id) {
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

    Result<size_t> WaitReasonManager::wake_one(size_t id) {
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
            clear_wait_metadata(tcb);
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

    Result<size_t> WaitReasonManager::wake_all(size_t id) {
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

            clear_wait_metadata(tcb);
            auto resume_res = dispatch_waiter(tcb);
            propagate(resume_res);
            ++count;
        }
        return count;
    }

    bool WaitReasonManager::has_waiting(size_t id) {
        auto qres = queue_if_exists(id);
        if (!qres.has_value()) {
            return false;
        }
        WaitQueue *queue = qres.value();
        return queue != nullptr && !queue->threads.empty();
    }

    size_t alloc_reason() {
        return WaitReasonManager::inst().alloc_reason();
    }

    Result<void> deprecated_wait_current(size_t id) {
        return schd::Scheduler::inst().block_current(id);
    }

    Result<void> deprecated_wait_current(size_t id,
                                         WaitPredicate predicate) {
        return schd::Scheduler::inst().block_current(id, std::move(predicate));
    }

    Result<TCB *> peek_one(size_t id) {
        return WaitReasonManager::inst().peek_one(id);
    }

    Result<size_t> wake_one(size_t id) {
        return WaitReasonManager::inst().wake_one(id);
    }

    Result<size_t> wake_all(size_t id) {
        return WaitReasonManager::inst().wake_all(id);
    }

    bool has_waiting(size_t id) {
        return WaitReasonManager::inst().has_waiting(id);
    }

}  // namespace task::wait
