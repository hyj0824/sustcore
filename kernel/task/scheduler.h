/**
 * @file scheduler.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief scheduler
 * @version alpha-1.0.0
 * @date 2026-03-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <schd/idle.h>
#include <schd/init.h>
#include <schd/rt.h>
#include <schd/schdbase.h>
#include <sus/nonnull.h>
#include <sustcore/errcode.h>
#include <task/task_struct.h>

namespace schd {
    using task::PCB;
    using task::TCB;

    void switch_pgd(TaskMemoryManager *tmm);

    class Scheduler {
    private:
        rt::RT<TCB> _rt_schd;
        rr::RR<TCB> _rr_schd;
        fcfs::FCFS<TCB> _fcfs_schd;
        idle::IDLE<TCB> _idle_schd;
        init::INIT<TCB> _init_schd;

    public:
        static void init(util::nonnull<TCB *> idle_tcb,
                         util::nonnull<TCB *> init_tcb);
        static bool initialized();
        static Scheduler &inst();

        constexpr Scheduler(util::nonnull<TCB *> idle_tcb,
                            util::nonnull<TCB *> init_tcb) {
            idle_tcb->basic_entity.state = ThreadState::READY;
            _idle_schd.ready             = &idle_tcb->basic_entity;
            init_tcb->basic_entity.state = ThreadState::READY;
            _init_schd.ready             = &init_tcb->basic_entity;
        }

        util::nonnull<RQ *> rq();

        constexpr util::nonnull<rt::RT<TCB> *> rt_schd() {
            return _rt_schd;
        }

        constexpr util::nonnull<rr::RR<TCB> *> rr_schd() {
            return _rr_schd;
        }

        constexpr util::nonnull<fcfs::FCFS<TCB> *> fcfs_schd() {
            return _fcfs_schd;
        }

        constexpr util::nonnull<idle::IDLE<TCB> *> idle_schd() {
            return _idle_schd;
        }

        constexpr util::nonnull<init::INIT<TCB> *> init_schd() {
            return _init_schd;
        }

        [[nodiscard]]
        TCB *current_tcb() const;

        /**
         * @brief 获取当前 hart 正在运行的进程.
         *
         * @return PCB* 当前进程
         */
        [[nodiscard]]
        PCB *current_pcb() const;

        using BaseSchedPtr = util::nonnull<BaseSched<TCB> *>;

        constexpr Result<BaseSchedPtr> schd(ClassType type) {
            switch (type) {
                case ClassType::RT:   return {rt_schd()};
                case ClassType::INIT: return {init_schd()};
                case ClassType::RR:   return {rr_schd()};
                case ClassType::FCFS: return {fcfs_schd()};
                case ClassType::IDLE: return {idle_schd()};
                default:              unexpect_return(ErrCode::INVALID_PARAM);
            }
        }

        /**
         * @brief 按优先级遍历调度器类
         *
         * 该函数会按照优先级顺序(从高到低)遍历调度器类,
         * 并对每个调度器类调用传入的函数对象
         * 当对应调度器类优先级低于指定的bot时, 遍历将会停止
         *
         * @tparam Func 函数对象类型, 期望类型为: BaseSchedPtr -> void
         * @param f 函数对象, 将会被调用来处理每个调度器类
         * @param bot 遍历的优先级下界, 默认为 ClassType::BOT,
         * 即遍历所有调度器类
         */
        template <typename Func>
        void foreach_schdclass(Func f, ClassType bot = ClassType::BOT) {
            if (ClassType::RT >= bot) {
                f(rt_schd());
            }
            if (ClassType::INIT >= bot) {
                f(init_schd());
            }
            if (ClassType::RR >= bot) {
                f(rr_schd());
            }
            if (ClassType::FCFS >= bot) {
                f(fcfs_schd());
            }
            if (ClassType::IDLE >= bot) {
                f(idle_schd());
            }
        }

        Result<util::nonnull<TCB *>> pick_next_task();
        Result<util::nonnull<TCB *>> prepare_next_task();
        Result<void> prepare_prev_task(TCB *tcb) noexcept;
        [[nodiscard]]
        bool can_schedule_tcb(TCB *tcb) noexcept;
        void check_preempt_curr(TCB *new_tcb);

        void prepare_switch(TCB *tcb);
        void switch_to(TCB *prev, TCB *next);

        bool try_wakeup(TCB *tcb, int flags);
        bool wakeup(TCB *tcb);

    public:
        void do_tick(const TimerTickEvent &e);

        void init();

        [[noreturn]]
        void bootstrap_tasks();

        /**
         * @brief 调度入口
         * 进入后将会判断是否需要进行调度,
         * 如果需要则选择下一个要运行的调度单元并切换到它.
         * 注意的是, 在 bootstrap_tasks() 之前 current_tcb/current_pcb
         * 均保持为空, 此时 schedule() 只会直接返回, 不会启动任务调度.
         *
         */
        void schedule();

        // 任务入队/唤醒
        Result<void> enqueue(util::nonnull<TCB *> tcb);

        // 任务出队/阻塞
        Result<void> dequeue(util::nonnull<TCB *> tcb);

        Result<void> block_current(size_t reason);
        Result<void> block_current(size_t reason,
                                   task::wait::WaitPredicate predicate);
        bool wakeup_waiting(TCB *tcb);

        // 唤醒新创建的任务并检查是否需要抢占当前任务
        bool wakeup_new(TCB *new_tcb);

        // 主动放弃 CPU
        void yield();
    };
}  // namespace schd
