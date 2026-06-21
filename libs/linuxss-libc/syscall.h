/**
 * @file syscall.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief linux subsystem libc syscall declarations
 * @version alpha-1.0.0
 * @date 2026-06-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <sustcore/capability.h>

extern "C" void sys_write_serial(const char *str, size_t len);
extern "C" bool sys_mem_resize(CapIdx idx, size_t newsz);
