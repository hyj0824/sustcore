/**
 * @file task_struct.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief
 * @version alpha-1.0.0
 * @date 2026-01-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <arch/description.h>
#include <cap/cholder.h>
#include <mem/vma.h>
#include <schd/fcfs.h>
#include <schd/rr.h>
#include <schd/schdbase.h>
#include <sus/list.h>
#include <sus/owner.h>
#include <sus/tree.h>
#include <sustcore/addr.h>
#include <syscall/syscall.h>

#include <coroutine>
#include <functional>

using tid_t        = size_t;
using pid_t        = size_t;
using WaitReasonId = size_t;

namespace task {
    struct TCB;
    struct PCB;
    using KThreadEntry = void (*)(void *);

    /**
     * @brief syscall 协程执行时使用的显式上下文.
     *
     * 轻量级内核协程不能依赖 current_tcb/current_pcb/tmm 等动态全局状态,
     * 因此所有 syscall 协程都必须通过该上下文访问其所属线程和地址空间.
     */
    struct SyscallContext {
        TCB *tcb                    = nullptr;
        PCB *pcb                    = nullptr;
        TaskMemoryManager *tmm      = nullptr;
        Context *trap_context       = nullptr;
    };

    namespace wait {
        using WaitPredicate      = std::function<bool(TCB *tcb)>;
        using WaitReadyPredicate = std::function<bool()>;
    }  // namespace wait

    // Make sure that TCB is has standard layout,
    // so that we can use offsetof to get the TCB pointer from the SU pointer.
    // TCB are arranged as a linked list
    struct TCB {
        /**
         * @brief 单个线程的 syscall 生命周期信息.
         */
        struct SyscallInfo {
            /**
             * @brief syscall 状态.
             */
            enum class State {
                NONE,
                EXECUTING,
                COMPLETED,
            };

            /// dispatcher 记录的 syscall 参数.
            syscall::ArgPack syscall_args{};
            /// 当前正在执行的 syscall 号.
            b64 syscall_number = 0;
            /// syscall 返回值缓存.
            syscall::RetPack syscall_result{};
            /// syscall 当前状态.
            State syscall_state = State::NONE;
            /// syscall coroutine 句柄.
            std::coroutine_handle<> handle = nullptr;
            /// syscall 协程的显式执行上下文.
            SyscallContext context{};

            /**
             * @brief 清空 syscall 生命周期状态.
             */
            void reset() noexcept;

            /**
             * @brief 以新的参数启动 syscall 生命周期.
             *
             * @param args 本次 syscall 的寄存器参数包.
             */
            void begin(const syscall::ArgPack &args) noexcept;

            /**
             * @brief 将 syscall 标记为完成并缓存返回值.
             *
             * @param ret syscall 返回结果包.
             */
            void complete(const syscall::RetPack &ret) noexcept;

            /**
             * @brief 判断当前 syscall 是否仍在执行中.
             */
            [[nodiscard]]
            bool executing() const noexcept {
                return syscall_state == State::EXECUTING;
            }

            /**
             * @brief 判断当前 syscall 是否已经完成.
             */
            [[nodiscard]]
            bool completed() const noexcept {
                return syscall_state == State::COMPLETED;
            }
        };

        // thread info
        tid_t tid;
        PCB *task;
        bool is_kernel;
        KThreadEntry kentry;
        void *karg;
        util::ListHead<TCB> list_head;

        // running information
        constexpr static size_t KSTACK_PAGES =
            4;  // 16KB(4 pages) for kernel stack
        constexpr static size_t KSTACK_SIZE = KSTACK_PAGES * PAGESIZE;
        void *kstack_top;
        PhyAddr kstack_phy;

        // get the pointer to the context of this thread
        Context *context() {
            return Context::from_kstack(kstack_top);
        }

        //  schedule data
        schd::ClassType schd_class;
        schd::SchedMeta basic_entity;
        schd::rr::Entity rr_entity;

        // wait data
        util::ListHead<TCB> wait_head;
        WaitReasonId wait_reason;
        // 等待谓词, 由等待的线程在进入等待时设置,
        // 由被等待的事件在满足条件时检查, 决定是否可以唤醒线程
        wait::WaitPredicate wait_predicate;
        SyscallInfo syscall_info;

        void *operator new(size_t size);
        void operator delete(void *ptr);
    };

    // PCB are arranged as a tree
    struct PCB : public util::tree_base::TreeBase<PCB> {
        // process info
        pid_t pid;
        bool is_kernel;
        int exit_code;
        bool exiting;
        // 是否已被加入回收队列
        bool recycle_queued;

        // the threads in this process
        util::IntrusiveList<TCB, &TCB::list_head> threads;

        // resources
        util::owner<TaskMemoryManager *> tmm;
        cap::CHolder *cholder;

        // initialization information
        VirAddr entrypoint;
        CapIdx pcb_cap;
        CapIdx main_tcb_cap;

        void *operator new(size_t size);
        void operator delete(void *ptr);
    };
}  // namespace task
