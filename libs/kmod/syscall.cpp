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
    sys_pcb_kill(__pcb_cap, exit_code);
    while (true) {}
}

CapIdx sys_create_process(CapIdx image_cap, size_t sched_class, CapIdx caps[],
                          const char *argv[], const char *envp[],
                          const char *bsargv[]) {
    return sys_pcb_create_process(__pcb_cap, image_cap, sched_class, caps,
                                  argv, envp, bsargv);
}

CapIdx sys_create_linux_process(CapIdx image_cap, size_t sched_class,
                                CapIdx caps[], const char *argv[],
                                const char *envp[], const char *bsargv[]) {
    return sys_pcb_create_linux_process(__pcb_cap, image_cap, sched_class,
                                        caps, argv, envp, bsargv);
}

CapIdx sys_create_thread(void (*entry)(), void *stack_addr,
                         size_t stack_size) {
    return sys_pcb_create_thread(__pcb_cap, entry, stack_addr, stack_size);
}

size_t fork(CapIdx *child_pcb_cap) {
    size_t child_pid = sys_pcb_fork(__pcb_cap, child_pcb_cap);
    // 子进程, 更新 pcb cap index
    if (child_pid == 0 && child_pcb_cap != nullptr) {
        __pcb_cap = *child_pcb_cap;
    }
    return child_pid;
}

bool sys_execve(CapIdx image_cap, CapIdx rsvdlst[], const char *argv[],
                const char *envp[], const char *bsargv[]) {
    return sys_pcb_execve(__pcb_cap, image_cap, rsvdlst, argv, envp, bsargv);
}

bool execve(CapIdx image_cap, CapIdx rsvdlst[], const char *argv[],
            const char *envp[], const char *bsargv[]) {
    return sys_execve(image_cap, rsvdlst, argv, envp, bsargv);
}

bool sys_mem_map(CapIdx idx, void *vaddr, uint64_t rwx, uint64_t growth) {
    return sys_pcb_map(__pcb_cap, idx, vaddr, rwx, growth);
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
