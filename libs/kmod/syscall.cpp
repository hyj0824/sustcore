/**
 * @file syscall.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief syscall function
 * @version alpha-1.0.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <kmod/syscall.h>
#include <prm.h>

#include <cstdio>
#include <cstring>

extern "C" {
void exit(int exit_code) {
    (void)sys_tcb_kill(__main_tcb_cap, exit_code);
    while (true) {}
}

SysRet<CapIdx> sys_create_process(size_t sched_class,
                                  const ExecveRequest *request) {
    return sys_pcb_create_process(__pcb_cap, sched_class, request);
}

SysRet<CapIdx> sys_create_linux_process(size_t sched_class,
                                        const ExecveRequest *request) {
    return sys_pcb_create_linux_process(__pcb_cap, sched_class, request);
}

SysRet<CapIdx> sys_create_thread(void (*entry)(), void *stack_addr,
                         size_t stack_size) {
    return sys_pcb_create_thread(__pcb_cap, entry, stack_addr, stack_size);
}

SysRet<size_t> fork(CapIdx *child_pcb_cap) {
    SysRet<size_t> fork_ret = sys_pcb_fork(__pcb_cap, child_pcb_cap);

    if (fork_ret.is_error()) {
        return fork_ret;
    }

    // 子进程, 更新 pcb cap index
    if (fork_ret.ret0 == 0 && child_pcb_cap != nullptr) {
        __pcb_cap = *child_pcb_cap;
    }
    return fork_ret;
}

SysRet<void> sys_execve(const ExecveRequest *request) {
    return sys_pcb_execve(__pcb_cap, request);
}

SysRet<void> execve(const ExecveRequest *request) {
    return sys_execve(request);
}

SysRet<void> sys_mem_map(CapIdx idx, void *vaddr, uint64_t rwx, uint64_t growth) {
    static_cast<void>(growth);
    MemQueryRet query{};
    if (!sys_mem_query(idx, &query)) {
        return SysRet<void> {.ret0 = 0, .ret1 = static_cast<size_t>(ErrCode::INVALID_CAPABILITY)};
    }
    return sys_pcb_map(__pcb_cap, idx, 0, vaddr, query.memsz, rwx);
}

size_t brk(size_t newbrk) {
    if (newbrk == 0) {
        return __current_brk;
    }

    if (newbrk < __heap_base) {
        return __current_brk;
    }
    if (!sys_mem_resize(__heap_mem_cap, newbrk - __heap_base)) {
        return __current_brk;
    }
    size_t actual_brk = newbrk;
    __current_brk     = actual_brk;
    return __current_brk;
}

void *sbrk(ptrdiff_t increment) {
    size_t old_brk = __current_brk;
    size_t newbrk  = old_brk;

    if (increment >= 0) {
        size_t inc = static_cast<size_t>(increment);
        if (SIZE_MAX - old_brk < inc) {
            return reinterpret_cast<void *>(-1);
        }
        newbrk = old_brk + inc;
    } else {
        size_t dec = size_t(0) - static_cast<size_t>(increment);
        if (old_brk < dec) {
            return reinterpret_cast<void *>(-1);
        }
        newbrk = old_brk - dec;
    }

    size_t actual_brk = brk(newbrk);
    if (actual_brk != newbrk) {
        return reinterpret_cast<void *>(-1);
    }
    return reinterpret_cast<void *>(old_brk);
}
}
