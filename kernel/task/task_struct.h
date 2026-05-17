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

#include <coroutine>
#include <functional>

using tid_t        = size_t;
using pid_t        = size_t;
using WaitReasonId = size_t;

namespace task {
    struct TCB;
    struct PCB;

    namespace wait {
        using WaitPredicate = std::function<bool(TCB *tcb)>;
    }

    // Make sure that TCB is has standard layout,
    // so that we can use offsetof to get the TCB pointer from the SU pointer.
    // TCB are arranged as a linked list
    struct TCB {
        // thread info
        tid_t tid;
        PCB *task;
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

        struct SystemCoroutines {
            // Endpoint IPC recv 协程句柄, 只由 endpoint recv/send 路径使用. 
            std::coroutine_handle<> ipc_handle = nullptr;
            // syscall 协程状态. pending 表示线程正在等待一个尚未返回用户态的
            // syscall; done 表示返回值已经写入上下文并且可以重新进入用户态. 
            bool syscall_pending               = false;
            bool syscall_done                  = true;
        };

        // wait data
        util::ListHead<TCB> wait_head;
        WaitReasonId wait_reason;
        // 等待谓词, 由等待的线程在进入等待时设置,
        // 由被等待的事件在满足条件时检查, 决定是否可以唤醒线程
        wait::WaitPredicate wait_predicate;
        SystemCoroutines coroutines;

        void *operator new(size_t size);
        void operator delete(void *ptr);
    };

    // PCB are arranged as a tree
    struct PCB : public util::tree_base::TreeBase<PCB> {
        // process info
        pid_t pid;
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
