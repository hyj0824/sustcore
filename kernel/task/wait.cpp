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

#include <env.h>
#include <device/int.h>
#include <mem/vma.h>
#include <task/scheduler.h>
#include <task/wait.h>

#include <cassert>
#include <coroutine>
#include <functional>

namespace task::wait {
    namespace key {
        struct wait : public env::key::tmm {
        public:
            wait() = default;
        };
    }  // namespace key

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static WaitReasonManager inst_wait_reason_manager;
    static bool inst_wait_reason_manager_initialized = false;
    static TCB *active_syscall_tcb                   = nullptr;

    WaitReasonManager &WaitReasonManager::inst() {
        if (!initialized()) {
            panic("WaitReasonManager 未初始化!");
        }
        return inst_wait_reason_manager;
    }

    void WaitReasonManager::init() {
        // call the constructor explicitly to ensure the instance is initialized
        // before use
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

        // Queue allocation is intentionally lazy: owning a reason id does not
        // consume a wait queue until a thread actually blocks on that reason.
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

        return _queues.at_nt(id)
            .transform(unwrap_ref<WaitQueue *>())
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
        tcb->wait_reason                = 0;
        tcb->wait_predicate             = {};
        tcb->coroutines.handle      = nullptr;
        tcb->coroutines.pending = false;
        tcb->coroutines.done    = true;
        tcb->wait_head.clear();
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
        tcb->wait_reason            = 0;
        tcb->wait_predicate         = {};
        tcb->coroutines.handle      = nullptr;
        tcb->coroutines.pending     = false;
        tcb->coroutines.done        = true;
        tcb->wait_head.clear();
        void_return();
    }

    static bool run_wait_predicate(TCB *tcb) {
        if (tcb == nullptr) {
            return false;
        }
        if (!tcb->wait_predicate) {
            return true;
        }

        auto *origin_tmm   = env::inst().tmm();
        PhyAddr origin_pgd = env::inst().pgd();
        auto *target_tmm   = tcb->task == nullptr ? nullptr : tcb->task->tmm;

        if (target_tmm != nullptr && target_tmm->pgd().nonnull() &&
            target_tmm->pgd() != origin_pgd)
        {
            env::inst().tmm(key::wait()) = target_tmm;
            PageMan(target_tmm->pgd()).switch_root();
            PageMan::flush_tlb();
        }

        bool should_wake = tcb->wait_predicate(tcb);

        if (target_tmm != nullptr && target_tmm->pgd().nonnull() &&
            target_tmm->pgd() != origin_pgd)
        {
            env::inst().tmm(key::wait()) = origin_tmm;
            PageMan(origin_pgd).switch_root();
            PageMan::flush_tlb();
        }

        return should_wake;
    }

    static bool has_syscall_waiter(TCB *tcb) {
        return tcb != nullptr && tcb->coroutines.handle;
    }

    static void clear_wait_metadata(TCB *tcb) {
        assert(tcb != nullptr);
        tcb->wait_reason                = 0;
        tcb->wait_predicate             = {};
        tcb->coroutines.handle      = nullptr;
        tcb->coroutines.pending = false;
        tcb->coroutines.done    = true;
        tcb->wait_head.clear();
    }

    static void clear_queue_metadata(TCB *tcb) {
        assert(tcb != nullptr);
        tcb->wait_reason    = 0;
        tcb->wait_predicate = {};
        tcb->wait_head.clear();
    }

    static void resume_syscall_waiter(TCB *tcb) {
        if (!has_syscall_waiter(tcb)) {
            return;
        }

        auto *origin_tmm   = env::inst().tmm();
        PhyAddr origin_pgd = env::inst().pgd();
        auto *target_tmm   = tcb->task == nullptr ? nullptr : tcb->task->tmm;

        if (target_tmm != nullptr && target_tmm->pgd().nonnull() &&
            target_tmm->pgd() != origin_pgd)
        {
            env::inst().tmm(key::wait()) = target_tmm;
            PageMan(target_tmm->pgd()).switch_root();
            PageMan::flush_tlb();
        }

        TCB *previous_active_syscall_tcb = active_syscall_tcb;
        active_syscall_tcb              = tcb;
        tcb->coroutines.handle.resume();
        active_syscall_tcb = previous_active_syscall_tcb;

        if (target_tmm != nullptr && target_tmm->pgd().nonnull() &&
            target_tmm->pgd() != origin_pgd)
        {
            env::inst().tmm(key::wait()) = origin_tmm;
            PageMan(origin_pgd).switch_root();
            PageMan::flush_tlb();
        }
    }

    bool resume_deferred_syscall(TCB *tcb) {
        if (!has_syscall_waiter(tcb)) {
            return true;
        }

        resume_syscall_waiter(tcb);
        if (tcb->coroutines.done) {
            clear_wait_metadata(tcb);
            return true;
        }

        return tcb->basic_entity.state != ThreadState::WAITING;
    }

    static size_t wake_waiter(TCB *tcb) {
        if (tcb == nullptr) {
            return 0;
        }

        if (has_syscall_waiter(tcb)) {
            clear_queue_metadata(tcb);
            return schd::Scheduler::inst().wakeup_waiting(tcb) ? 1 : 0;
        }

        clear_wait_metadata(tcb);
        return schd::Scheduler::inst().wakeup_waiting(tcb) ? 1 : 0;
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
                return wake_waiter(tcb);
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

            count += wake_waiter(tcb);
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

    namespace {
        /**
         * @brief wait_current内部使用的协程挂起点.
         *
         * 该类型不作为公开API暴露; 调用方只能通过wait_current协程等待.
         */
        class CoroutineWait {
        private:
            WaitReasonId _reason = 0;
            WaitPredicate _predicate;
            WaitReadyPredicate _ready_predicate;
            Result<void> _result{};

        public:
            explicit CoroutineWait(WaitReasonId reason,
                                   WaitPredicate predicate = {},
                                   WaitReadyPredicate ready_predicate = {})
                : _reason(reason), _predicate(std::move(predicate)),
                  _ready_predicate(std::move(ready_predicate)) {}

            [[nodiscard]]
            bool await_ready() const {
                return false;
            }

            bool await_suspend(std::coroutine_handle<> handle) {
                auto *tcb = active_syscall_tcb != nullptr
                                ? active_syscall_tcb
                                : schd::Scheduler::inst().current_tcb();
                if (tcb == nullptr ||
                    tcb->schd_class == schd::ClassType::IDLE)
                {
                    _result = std::unexpected(ErrCode::INVALID_PARAM);
                    return false;
                }

                tcb->coroutines.handle = handle;

                InterruptGuard guard;
                guard.enter();
                if (_ready_predicate && _ready_predicate()) {
                    tcb->coroutines.handle = nullptr;
                    return false;
                }

                _result = WaitReasonManager::inst().enqueue(
                    _reason, tcb, std::move(_predicate));
                if (!_result.has_value()) {
                    tcb->coroutines.handle = nullptr;
                    return false;
                }
                tcb->basic_entity
                    .template flags_set<schd::SchedMeta::FLAGS_NEED_RESCHED>();
                return true;
            }

            Result<void> await_resume() const {
                return _result;
            }
        };
    }  // namespace

    util::cotask<Result<void>> wait_current(WaitReasonId id) {
        co_return co_await CoroutineWait(id);
    }

    util::cotask<Result<void>> wait_current(WaitReasonId id,
                                            WaitPredicate predicate) {
        co_return co_await CoroutineWait(id, std::move(predicate));
    }

    util::cotask<Result<void>> wait_current(
        WaitReasonId id, WaitPredicate predicate,
        WaitReadyPredicate ready_predicate) {
        co_return co_await CoroutineWait(id, std::move(predicate),
                                         std::move(ready_predicate));
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
