/**
 * @file rt.h
 * @brief real-time FIFO scheduler
 */

#pragma once

#include <schd/fcfs.h>
#include <schd/schdbase.h>

namespace schd::rt {
    template <typename SU>
    class RT : public fcfs::FCFS<SU> {
    public:
        using SUType                          = SU;
        constexpr static ClassType CLASS_TYPE = ClassType::RT;

        Result<void> enqueue(util::nonnull<RQ *> rq,
                             util::nonnull<SUType *> unit) override {
            auto meta   = this->asmeta(unit);
            meta->state = ThreadState::READY;
            rq->rt_list.push_back(*meta);
            void_return();
        }

        Result<void> dequeue(util::nonnull<RQ *> rq,
                             util::nonnull<SUType *> unit) override {
            auto meta = this->asmeta(unit);
            if (!rq->rt_list.contains(*meta)) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            rq->rt_list.remove(*meta);
            meta->state = ThreadState::EMPTY;
            void_return();
        }

        Result<util::nonnull<SUType *>> pick_next(
            util::nonnull<RQ *> rq) override {
            if (rq->rt_list.empty()) {
                unexpect_return(ErrCode::NO_RUNNABLE_THREAD);
            }
            SchedMeta &meta = rq->rt_list.front();
            meta.state      = ThreadState::RUNNING;
            rq->rt_list.pop_front();
            this->cursched = &meta;
            return this->asunit(meta);
        }

        Result<void> put_prev(util::nonnull<RQ *> rq,
                              util::nonnull<SUType *> unit) override {
            auto meta   = this->asmeta(unit);
            meta->state = ThreadState::READY;
            rq->rt_list.push_back(*meta);
            void_return();
        }
    };
}  // namespace schd::rt
