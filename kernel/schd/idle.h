/**
 * @file fcfs.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief first come first serve scheduler
 * @version alpha-1.0.0
 * @date 2026-03-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <schd/schdbase.h>
#include <sus/list.h>
#include <sus/nonnull.h>
#include <sustcore/errcode.h>

namespace schd::idle {
    template <typename SU>
    class IDLE : public BaseSched<SU> {
    public:
        using SUType                          = SU;
        constexpr static ClassType CLASS_TYPE = ClassType::IDLE;
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
            // 为当前进程添加 NEED_RESCHED 标志
            if (this->cursched != nullptr) {
                this->cursched
                    ->template flags_set<SchedMeta::FLAGS_NEED_RESCHED>();
            }
            void_return();
        }

        Result<void> on_tick(util::nonnull<RQ *> rq,
                             util::nonnull<SUType *> unit) override {
            // IDLE 不需要在时钟中断时进行任何操作
            void_return();
        }

        bool check_preempt_curr(util::nonnull<RQ *> rq, util::nonnull<SUType *> new_su) override {
            // 只要有新的调度单元要运行, 就需要抢占当前的 IDLE 线程
            return true;
        }
    };
}  // namespace schd::idle
