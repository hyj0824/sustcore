/**
 * @file init.h
 * @brief init scheduler
 */

#pragma once

#include <schd/schdbase.h>
#include <sus/nonnull.h>
#include <sustcore/errcode.h>
#include <task/task_struct.h>

namespace schd::init {
    template <typename SU>
    class INIT : public BaseSched<SU> {
    public:
        using SUType                          = SU;
        constexpr static ClassType CLASS_TYPE = ClassType::INIT;
        SchedMeta *kinit_ready = nullptr;
        SchedMeta *init_ready  = nullptr;

    private:
        [[nodiscard]]
        Result<SchedMeta **> ready_slot(util::nonnull<SUType *> unit) noexcept {
            switch (unit->boot_role) {
                case task::BootThreadRole::KINIT:     return &kinit_ready;
                case task::BootThreadRole::INIT_USER: return &init_ready;
                case task::BootThreadRole::NONE:
                default: unexpect_return(ErrCode::INVALID_PARAM);
            }
        }

    public:

        Result<void> enqueue(util::nonnull<RQ *> rq,
                             util::nonnull<SUType *> unit) override {
            auto slot_res = ready_slot(unit);
            propagate(slot_res);
            auto meta   = this->asmeta(unit);
            meta->state = ThreadState::READY;
            *slot_res.value() = meta.get();
            void_return();
        }

        Result<void> dequeue(util::nonnull<RQ *> rq,
                             util::nonnull<SUType *> unit) override {
            auto slot_res = ready_slot(unit);
            propagate(slot_res);
            auto meta = this->asmeta(unit);
            if (*slot_res.value() != meta.get()) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            *slot_res.value() = nullptr;
            meta->state = ThreadState::EMPTY;
            void_return();
        }

        Result<util::nonnull<SUType *>> pick_next(
            util::nonnull<RQ *> rq) override {
            SchedMeta *ready = kinit_ready != nullptr ? kinit_ready : init_ready;
            if (ready == nullptr) {
                unexpect_return(ErrCode::NO_RUNNABLE_THREAD);
            }
            SchedMeta *meta = ready;
            if (meta == kinit_ready) {
                kinit_ready = nullptr;
            } else {
                init_ready = nullptr;
            }
            meta->state     = ThreadState::RUNNING;
            this->cursched  = meta;
            return this->asunit(util::nnullforce(meta));
        }

        Result<void> put_prev(util::nonnull<RQ *> rq,
                              util::nonnull<SUType *> unit) override {
            auto slot_res = ready_slot(unit);
            propagate(slot_res);
            auto meta   = this->asmeta(unit);
            meta->state = ThreadState::READY;
            *slot_res.value() = meta.get();
            void_return();
        }

        Result<void> yield(util::nonnull<RQ *> rq) override {
            if (this->cursched != nullptr) {
                this->cursched
                    ->template flags_set<SchedMeta::FLAGS_NEED_RESCHED>();
            }
            void_return();
        }

        Result<void> on_tick(util::nonnull<RQ *> rq,
                             util::nonnull<SUType *> unit) override {
            void_return();
        }

        bool check_preempt_curr(util::nonnull<RQ *> rq,
                                util::nonnull<SUType *> new_su) override {
            return true;
        }
    };
}  // namespace schd::init
