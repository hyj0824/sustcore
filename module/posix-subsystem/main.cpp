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

#include <cstddef>

extern "C" void sys_write_serial(const char *str, size_t len);

namespace {
    volatile bool g_posix_initialized = false;

    size_t strlen(const char *str) {
        size_t len = 0;
        while (str[len] != '\0') {
            ++len;
        }
        return len;
    }

    void serial_write(const char *str, size_t len) {
        sys_write_serial(str, len);
    }
}  // namespace

extern "C" void posix_init();
extern "C" void posix_dispatch();

extern "C" size_t posix_ss_main(size_t a0, size_t a1, size_t a2, size_t a3,
                                size_t a4, size_t a5, size_t a6, size_t a7) {
    if (g_posix_initialized) {
        posix_dispatch();
    } else {
        posix_init();
    }
    return a0;
}

extern "C" void posix_init() {
    g_posix_initialized = true;
    const char *msg     = "posix-subsystem: init\n";
    serial_write(msg, strlen(msg));
}

extern "C" void posix_dispatch() {
    const char *msg = "POSIX SYSTEM CALL TRIGGERED!\n";
    serial_write(msg, strlen(msg));
}
