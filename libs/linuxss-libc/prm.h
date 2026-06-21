/**
 * @file prm.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief linux subsystem libc runtime parameters
 * @version alpha-1.0.0
 * @date 2026-06-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <elf.h>

#include <sustcore/bootstrap.h>
#include <sustcore/capability.h>

extern "C" bool g_linux_initialized;
extern size_t __linuxss_heap_base;
extern size_t __linuxss_ssheap_base;
extern size_t __linuxss_brk;
extern size_t __linuxss_ss_brk;
extern CapIdx __linuxss_pcb_cap;
extern CapIdx __linuxss_main_tcb_cap;
extern CapIdx __linuxss_heap_mem_cap;
extern CapIdx __linuxss_ssheap_mem_cap;
extern size_t __linuxss_bsargc;
extern const bsheader **__linuxss_bsargv;

extern "C" void linuxss_restore_runtime_from_bootstrap(
    size_t bsargc, const bsheader *bsargv[]);
extern "C" size_t linuxss_brk(size_t newbrk);
extern "C" size_t linuxss_entry(const void *stack_sp, size_t init_a0,
                                size_t init_a1, size_t init_a2);
extern "C" void linux_main(const void *stack_sp, size_t argc, const char *argv[],
                           const char *envp[], const Elf64_auxv_t *auxv,
                           size_t bsargc, const bsheader *bsargv[]);
extern "C" size_t linux_dispatch(size_t a0, size_t a1, size_t a2, size_t a3,
                                 size_t a4, size_t a5, size_t a6, size_t a7);
