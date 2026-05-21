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
#include <sustcore/capability.h>

#include <cstddef>
#include <cstring>

typedef void (*_init_func)(void);

extern _init_func __init_array_start[0], __init_array_end[0];

size_t __heap_base;
size_t __current_brk;
CapIdx __pcb_cap;
CapIdx __main_tcb_cap;
CapIdx __heap_mem_cap;
CapIdx __stack_mem_cap;

namespace kmod {
    void init(void) {
        const size_t count1 = __init_array_end - __init_array_start;

        // 执行init
        for (size_t i = 0; i < count1; i++) {
            __init_array_start[i]();
        }
    }
}  // namespace kmod

void kmod_main(void);

extern "C" void _cpp_setup(size_t heap_vaddr, CapIdx pcb_cap,
                           CapIdx main_tcb_cap, CapIdx heap_mem_cap,
                           CapIdx stack_mem_cap) {
    __heap_base     = heap_vaddr;
    __current_brk   = heap_vaddr;
    __pcb_cap       = pcb_cap;
    __main_tcb_cap  = main_tcb_cap;
    __heap_mem_cap  = heap_mem_cap;
    __stack_mem_cap = stack_mem_cap;

    kmod::init();
    kmod_main();
    while (true);
}
