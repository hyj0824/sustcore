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

    template <typename T>
    Result<void> Promise<T>::set_value(T value) {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr || _state->state != FutureState::PENDING) {
            unexpect_return(ErrCode::BUSY);
        }

        _state->value = std::move(value);
        _state->state = FutureState::COMPLETE;
        auto wake_res = wake_all(_state->wait_reason);
        propagate(wake_res);
        void_return();
    }

    template <typename T>
    Result<void> Promise<T>::set_error(ErrCode error) noexcept {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr || _state->state != FutureState::PENDING) {
            unexpect_return(ErrCode::BUSY);
        }
        _state->error = error;
        _state->state = FutureState::ERROR;
        auto wake_res = wake_all(_state->wait_reason);
        propagate(wake_res);
        void_return();
    }

    template <typename T>
    Result<void> Promise<T>::set_cancled() noexcept {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr || _state->state != FutureState::PENDING) {
            unexpect_return(ErrCode::BUSY);
        }
        _state->state = FutureState::CANCLED;
        auto wake_res = wake_all(_state->wait_reason);
        propagate(wake_res);
        void_return();
    }

    Result<void> Promise<void>::set_value() {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr || _state->state != FutureState::PENDING) {
            unexpect_return(ErrCode::BUSY);
        }
        _state->state = FutureState::COMPLETE;
        auto wake_res = wake_all(_state->wait_reason);
        propagate(wake_res);
        void_return();
    }

    Result<void> Promise<void>::set_error(ErrCode error) noexcept {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr || _state->state != FutureState::PENDING) {
            unexpect_return(ErrCode::BUSY);
        }
        _state->error = error;
        _state->state = FutureState::ERROR;
        auto wake_res = wake_all(_state->wait_reason);
        propagate(wake_res);
        void_return();
    }

    Result<void> Promise<void>::set_cancled() noexcept {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr || _state->state != FutureState::PENDING) {
            unexpect_return(ErrCode::BUSY);
        }
        _state->state = FutureState::CANCLED;
        auto wake_res = wake_all(_state->wait_reason);
        propagate(wake_res);
        void_return();
    }

    template <typename T>
    Result<void> Future<T>::cancle() noexcept {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        switch (_state->state) {
            case FutureState::PENDING: {
                if (_state->cancel_callback) {
                    auto cancel_res = _state->cancel_callback();
                    propagate(cancel_res);
                }
                _state->state = FutureState::CANCLED;
                auto wake_res = wake_all(_state->wait_reason);
                propagate(wake_res);
                void_return();
            }
            case FutureState::COMPLETE: unexpect_return(ErrCode::FUTURE_ERROR);
            case FutureState::ERROR:    unexpect_return(ErrCode::FUTURE_ERROR);
            case FutureState::CANCLED:  unexpect_return(ErrCode::FUTURE_CANCLED);
            case FutureState::CONSUMED:
                unexpect_return(ErrCode::FUTURE_CONSUMED);
        }
        unexpect_return(ErrCode::UNKNOWN_ERROR);
    }

    Result<void> Future<void>::cancle() noexcept {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        switch (_state->state) {
            case FutureState::PENDING: {
                if (_state->cancel_callback) {
                    auto cancel_res = _state->cancel_callback();
                    propagate(cancel_res);
                }
                _state->state = FutureState::CANCLED;
                auto wake_res = wake_all(_state->wait_reason);
                propagate(wake_res);
                void_return();
            }
            case FutureState::COMPLETE: unexpect_return(ErrCode::FUTURE_ERROR);
            case FutureState::ERROR:    unexpect_return(ErrCode::FUTURE_ERROR);
            case FutureState::CANCLED:  unexpect_return(ErrCode::FUTURE_CANCLED);
            case FutureState::CONSUMED:
                unexpect_return(ErrCode::FUTURE_CONSUMED);
        }
        unexpect_return(ErrCode::UNKNOWN_ERROR);
    }

    template <typename T>
    typename Future<T>::wait_result_type Future<T>::value() {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        switch (_state->state) {
            case FutureState::PENDING:   unexpect_return(ErrCode::FUTURE_PENDING);
            case FutureState::ERROR:     unexpect_return(_state->error);
            case FutureState::CANCLED:   unexpect_return(ErrCode::FUTURE_CANCLED);
            case FutureState::CONSUMED:  unexpect_return(ErrCode::FUTURE_CONSUMED);
            case FutureState::COMPLETE: {
                if (!_state->value.has_value()) {
                    unexpect_return(ErrCode::FUTURE_ERROR);
                }
                auto value = std::move(_state->value.value());
                _state->value.reset();
                _state->state = FutureState::CONSUMED;
                if constexpr (detail::is_result_type_v<T>) {
                    return value;
                } else {
                    return value;
                }
            }
        }
        unexpect_return(ErrCode::UNKNOWN_ERROR);
    }

    Result<void> Future<void>::value() {
        InterruptGuard guard;
        guard.enter();
        if (_state == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        switch (_state->state) {
            case FutureState::PENDING:  unexpect_return(ErrCode::FUTURE_PENDING);
            case FutureState::ERROR:    unexpect_return(_state->error);
            case FutureState::CANCLED:  unexpect_return(ErrCode::FUTURE_CANCLED);
            case FutureState::CONSUMED: unexpect_return(ErrCode::FUTURE_CONSUMED);
            case FutureState::COMPLETE:
                _state->state = FutureState::CONSUMED;
                void_return();
        }
        unexpect_return(ErrCode::UNKNOWN_ERROR);
    }

    template <typename T>
    typename Future<T>::wait_result_type Future<T>::awaiter::await_resume() {
        if (future == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return take_wait_result(*future);
    }

    template <typename T>
    typename detail::future_wait_result_t<T> wait_for(Future<T> &future) {
        auto current_res = check_waiting_thread(false);
        propagate(current_res);
        while (!future.readable()) {
            auto wait_res =
                schd::Scheduler::inst().block_current(future.wait_reason());
            propagate(wait_res);
        }
        return take_wait_result(future);
    }

    template <typename T>
    typename detail::future_wait_result_t<T> kthread_wait_for(Future<T> &future) {
        auto current_res = check_waiting_thread(true);
        propagate(current_res);
        while (!future.readable()) {
            auto wait_res =
                schd::Scheduler::inst().block_current(future.wait_reason());
            propagate(wait_res);
        }
        return take_wait_result(future);
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

    template Result<void> Promise<int>::set_value(int value);
    template Result<void> Promise<int>::set_error(ErrCode error) noexcept;
    template Result<void> Promise<int>::set_cancled() noexcept;
    template Result<void> Future<int>::cancle() noexcept;
    template typename Future<int>::wait_result_type
        Future<int>::awaiter::await_resume();
    template typename Future<int>::wait_result_type Future<int>::value();
    template Result<int> wait_for(Future<int> &future);
    template Result<int> kthread_wait_for(Future<int> &future);

    template Result<void> Promise<bool>::set_value(bool value);
    template Result<void> Promise<bool>::set_error(ErrCode error) noexcept;
    template Result<void> Promise<bool>::set_cancled() noexcept;
    template Result<void> Future<bool>::cancle() noexcept;
    template typename Future<bool>::wait_result_type
        Future<bool>::awaiter::await_resume();
    template typename Future<bool>::wait_result_type Future<bool>::value();
    template Result<bool> wait_for(Future<bool> &future);
    template Result<bool> kthread_wait_for(Future<bool> &future);

    template Result<void> Promise<Result<int>>::set_value(Result<int> value);
    template Result<void> Promise<Result<int>>::set_error(ErrCode error) noexcept;
    template Result<void> Promise<Result<int>>::set_cancled() noexcept;
    template Result<void> Future<Result<int>>::cancle() noexcept;
    template typename Future<Result<int>>::wait_result_type
        Future<Result<int>>::awaiter::await_resume();
    template typename Future<Result<int>>::wait_result_type
        Future<Result<int>>::value();
    template Result<int> wait_for(Future<Result<int>> &future);
    template Result<int> kthread_wait_for(Future<Result<int>> &future);

    template Result<void> Promise<cap::EndpointMessage *>::set_value(
        cap::EndpointMessage *value);
    template Result<void> Promise<cap::EndpointMessage *>::set_error(
        ErrCode error) noexcept;
    template Result<void> Promise<cap::EndpointMessage *>::set_cancled() noexcept;
    template Result<void> Future<cap::EndpointMessage *>::cancle() noexcept;
    template typename Future<cap::EndpointMessage *>::wait_result_type
        Future<cap::EndpointMessage *>::awaiter::await_resume();
    template typename Future<cap::EndpointMessage *>::wait_result_type
        Future<cap::EndpointMessage *>::value();
    template Result<cap::EndpointMessage *>
        wait_for(Future<cap::EndpointMessage *> &future);
    template Result<cap::EndpointMessage *>
        kthread_wait_for(Future<cap::EndpointMessage *> &future);

    template Result<void> wait_for(Future<void> &future);
    template Result<void> kthread_wait_for(Future<void> &future);

}  // namespace task::wait
