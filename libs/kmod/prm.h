/**
 * @file prm.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief paramaters
 * @version alpha-1.0.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <kmod/bootstrap.h>
#include <sustcore/startup.h>
#include <sustcore/capability.h>

#include <cstddef>

extern size_t __heap_base;
extern size_t __current_brk;
extern CapIdx __pcb_cap;
extern CapIdx __main_tcb_cap;
extern CapIdx __heap_mem_cap;
extern CapIdx __stack_mem_cap;
extern size_t __argc;
extern const char **__argv;
extern const char **__envp;
extern const task::KmodAuxvEntry *__auxv;
extern size_t __bsargc;
extern const bsheader **__bsargv;
