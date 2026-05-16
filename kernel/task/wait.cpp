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
#include <mem/vma.h>
#include <task/scheduler.h>
#include <task/wait.h>

#include <cassert>

namespace task::wait {
    namespace key {
        struct wait : public env::key::tmm {
        public:
            wait() = default;
        };
    }  // namespace key

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static WaitReasonManager inst_wait_reason_manager;

    WaitReasonManager &WaitReasonManager::inst() {
        return inst_wait_reason_manager;
    }

    void WaitReasonManager::init() {
        // call the constructor explicitly to ensure the instance is initialized
        // before use
        new (&inst_wait_reason_manager) WaitReasonManager();
    }

    WaitReasonId WaitReasonManager::alloc_reason() {
        return _next_reason++;
    }

    Result<WaitQueue *> WaitReasonManager::queue_for_wait(WaitReasonId id) {
        if (id == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto qres = _queues.get(id);
        if (qres.has_value()) {
            return qres.value().get();
        }

        // Queue allocation is intentionally lazy: owning a reason id does not
        // consume a wait queue until a thread actually blocks on that reason.
        auto *queue = new WaitQueue(id);
        if (queue == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        _queues.put(id, queue);
        return queue;
    }

    Result<WaitQueue *> WaitReasonManager::queue_if_exists(WaitReasonId id) {
        if (id == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto qres = _queues.get(id);
        if (!qres.has_value()) {
            // Waking an unused reason is a no-op, not an error.
            return static_cast<WaitQueue *>(nullptr);
        }
        return qres.value().get();
    }

    Result<void> WaitReasonManager::enqueue(WaitReasonId id, TCB *tcb) {
        return enqueue(id, tcb, {});
    }

    Result<void> WaitReasonManager::enqueue(WaitReasonId id, TCB *tcb,
                                            WakePostAction action) {
        if (tcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        auto qres = queue_for_wait(id);
        propagate(qres);

        tcb->wait_reason        = id;
        tcb->wait_post_action   = std::move(action);
        tcb->basic_entity.state = ThreadState::WAITING;
        qres.value()->threads.push_back(*tcb);
        void_return();
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
        tcb->wait_reason        = 0;
        tcb->wait_post_action   = {};
        tcb->wait_head.clear();
        return tcb;
    }

    static bool run_post_action(TCB *tcb) {
        if (tcb == nullptr) {
            return false;
        }
        if (!tcb->wait_post_action) {
            return true;
        }

        auto *origin_tmm = env::inst().tmm();
        PhyAddr origin_pgd = env::inst().pgd();
        auto *target_tmm = tcb->task == nullptr ? nullptr : tcb->task->tmm;

        if (target_tmm != nullptr && target_tmm->pgd().nonnull() &&
            target_tmm->pgd() != origin_pgd) {
            env::inst().tmm(key::wait()) = target_tmm;
            PageMan(target_tmm->pgd()).switch_root();
            PageMan::flush_tlb();
        }

        bool should_wake = tcb->wait_post_action(tcb);

        if (target_tmm != nullptr && target_tmm->pgd().nonnull() &&
            target_tmm->pgd() != origin_pgd) {
            env::inst().tmm(key::wait()) = origin_tmm;
            PageMan(origin_pgd).switch_root();
            PageMan::flush_tlb();
        }

        return should_wake;
    }

    static void clear_wait_metadata(TCB *tcb) {
        assert(tcb != nullptr);
        tcb->wait_reason        = 0;
        tcb->wait_post_action   = {};
        tcb->wait_head.clear();
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

            if (run_post_action(tcb)) {
                clear_wait_metadata(tcb);
                return schd::Scheduler::inst().wakeup_waiting(tcb) ? size_t(1)
                                                                   : size_t(0);
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
        auto qres = queue_if_exists(id);
        propagate(qres);

        WaitQueue *queue = qres.value();
        if (queue == nullptr || queue->threads.empty()) {
            return count;
        }

        size_t scan_count = queue->threads.size();
        for (size_t i = 0; i < scan_count; ++i) {
            TCB *tcb = &queue->threads.front();
            queue->threads.pop_front();

            if (!run_post_action(tcb)) {
                queue->threads.push_back(*tcb);
                continue;
            }

            clear_wait_metadata(tcb);
            if (schd::Scheduler::inst().wakeup_waiting(tcb)) {
                ++count;
            }
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

    Result<void> wait_current(WaitReasonId id) {
        return schd::Scheduler::inst().block_current(id);
    }

    Result<void> wait_current(WaitReasonId id, WakePostAction action) {
        return schd::Scheduler::inst().block_current(id, std::move(action));
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
