/**
 * @file syscall.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief posix subsystem libc syscall helpers
 * @version alpha-1.0.0
 * @date 2026-06-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <sustcore/syscall.h>

#include <cstddef>

extern "C" void sys_write_serial(const char *str, size_t len) {
#if defined(__ARCH_riscv64__)
    register size_t a0 asm("a0")      = 0;
    register const char *a1 asm("a1") = str;
    register size_t a2 asm("a2")      = len;
    register size_t a7 asm("a7")      = SYS_WRITE_SERIAL;
    asm volatile("ecall"
                 : "+r"(a0)
                 : "r"(a1), "r"(a2), "r"(a7)
                 : "memory");
#else
    (void)str;
    (void)len;
#endif
}
