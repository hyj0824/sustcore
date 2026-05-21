/**
 * @file init.h
 * @brief init scheduler
 */

#pragma once

#include <schd/schdbase.h>
#include <sus/nonnull.h>
#include <sustcore/errcode.h>

namespace schd::init {
    template <typename SU>
    class INIT : public BaseSched<SU> {
    public:
        using SUType                          = SU;
        constexpr static ClassType CLASS_TYPE = ClassType::INIT;
        SchedMeta *ready = nullptr;

        Result<void> enqueue(util::nonnull<RQ *> rq,
                             util::nonnull<SUType *> unit) override {
            auto meta   = this->asmeta(unit);
            meta->state = ThreadState::READY;
            ready       = meta.get();
            void_return();
        }

        Result<void> dequeue(util::nonnull<RQ *> rq,
                             util::nonnull<SUType *> unit) override {
            auto meta = this->asmeta(unit);
            if (ready != meta.get()) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            ready       = nullptr;
            meta->state = ThreadState::EMPTY;
            void_return();
        }

        Result<util::nonnull<SUType *>> pick_next(
            util::nonnull<RQ *> rq) override {
            if (ready == nullptr) {
                unexpect_return(ErrCode::NO_RUNNABLE_THREAD);
            }
            SchedMeta *meta = ready;
            ready           = nullptr;
            meta->state     = ThreadState::RUNNING;
            this->cursched  = meta;
            return this->asunit(util::nnullforce(meta));
        }

        Result<void> put_prev(util::nonnull<RQ *> rq,
                              util::nonnull<SUType *> unit) override {
            auto meta   = this->asmeta(unit);
            meta->state = ThreadState::READY;
            ready       = meta.get();
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
