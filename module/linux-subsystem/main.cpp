/**
 * @file main.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief POSIX subsystem main file
 * @version alpha-1.0.0
 * @date 2026-06-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <sus/types.h>
#include <syscall.h>

#include <cstddef>

volatile bool g_linux_initialized = false;

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len] != '\0') {
        ++len;
    }
    return len;
}

size_t puts(const char *str) {
    size_t len = strlen(str);
    sys_write_serial(str, len);
    return len;
}

extern "C" void linux_init();
extern "C" size_t linux_dispatch(size_t a0, size_t a1, size_t a2, size_t a3,
                                 size_t a4, size_t a5, size_t a6, size_t a7);

extern "C" size_t linux_ss_main(size_t a0, size_t a1, size_t a2, size_t a3,
                                size_t a4, size_t a5, size_t a6, size_t a7) {
    if (!g_linux_initialized) {
        linux_init();
        return 0;
    }
    return linux_dispatch(a0, a1, a2, a3, a4, a5, a6, a7);
}

extern "C" void linux_init() {
    g_linux_initialized = true;
    puts("linux-subsystem: initialized\n");
}

#define LINUX_SYS_WRITE 64
#define INVALID_VALUE   0xFFFF'FFFF'FFFF'FFFF

size_t linux_sys_write(size_t fd, const void *buf, size_t len) {
    if (fd == 1) {
        sys_write_serial(reinterpret_cast<const char *>(buf), len);
        return len;
    }
    puts("linux-subsystem: unsupported fd\n");
    return INVALID_VALUE;
}

extern "C" size_t linux_dispatch(size_t a0, size_t a1, size_t a2, size_t a3,
                                 size_t a4, size_t a5, size_t a6, size_t a7) {
    switch (a7) {
        case LINUX_SYS_WRITE:
            return linux_sys_write(a0, reinterpret_cast<const void *>(a1), a2);
        default:
            puts("linux-subsystem: unknown syscall\n");
            return INVALID_VALUE;
    }
}
