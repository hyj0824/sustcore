/**
 * @file setup.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief setup function
 * @version alpha-1.0.0
 * @date 2026-02-23
 *
 * @copyright Copyright (c) 2026
 *
 */

// cpp setup入口点

#include <prm.h>
#include <sustcore/startup.h>
#include <sustcore/capability.h>
#include <kmod/syscall.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

typedef void (*_init_func)(void);

extern _init_func __init_array_start[0], __init_array_end[0];

size_t __heap_base;
size_t __current_brk;
CapIdx __pcb_cap;
CapIdx __main_tcb_cap;
CapIdx __heap_mem_cap;
CapIdx __stack_mem_cap;
size_t __argc;
const char **__argv;
const char **__envp;
const task::KmodAuxvEntry *__auxv;
size_t __bsargc;
const bsheader **__bsargv;

namespace kmod {
    void init(void) {
        const size_t count1 = __init_array_end - __init_array_start;

        // 执行init
        for (size_t i = 0; i < count1; i++) {
            __init_array_start[i]();
        }
    }
}  // namespace kmod

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
                         const bsheader *bsargv[]);

extern "C" void _cpp_setup(const void *stack_start) {
    if (stack_start == nullptr) {
        while (true) {}
    }

    auto *words = static_cast<const uint64_t *>(stack_start);
    __argc      = static_cast<size_t>(words[0]);
    __argv      = reinterpret_cast<const char **>(
        const_cast<uint64_t *>(words + 1));

    size_t offset = 1 + __argc + 1;
    __envp        = reinterpret_cast<const char **>(
        const_cast<uint64_t *>(words + offset));
    while (words[offset] != 0) {
        ++offset;
    }
    ++offset;

    __auxv = reinterpret_cast<const task::KmodAuxvEntry *>(words + offset);
    while (true) {
        auto *entry = reinterpret_cast<const task::KmodAuxvEntry *>(
            words + offset);
        offset += 2;
        if (entry->a_type == task::KMOD_AT_NULL) {
            break;
        }
        switch (entry->a_type) {
            case task::KMOD_AT_SUS_HEAP_BASE:
                __heap_base   = entry->a_val;
                __current_brk = entry->a_val;
                break;
            case task::KMOD_AT_SUS_PCB_CAP:
                __pcb_cap = entry->a_val;
                break;
            case task::KMOD_AT_SUS_MAIN_TCB:
                __main_tcb_cap = entry->a_val;
                break;
            case task::KMOD_AT_SUS_HEAP_MEM:
                __heap_mem_cap = entry->a_val;
                break;
            case task::KMOD_AT_SUS_STACK_MEM:
                __stack_mem_cap = entry->a_val;
                break;
            default: break;
        }
    }

    __bsargc = static_cast<size_t>(words[offset]);
    ++offset;
    __bsargv = reinterpret_cast<const bsheader **>(
        const_cast<uint64_t *>(words + offset));

    kmod::init();

    kmod_main(static_cast<int>(__argc), __argv, __envp, __bsargv);
    while (true);
}
