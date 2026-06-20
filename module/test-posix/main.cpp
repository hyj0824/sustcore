/**
 * @file main.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief posix subsystem testing program
 * @version alpha-1.0.0
 * @date 2026-06-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cstddef>

namespace {
    constexpr size_t STDOUT_FD          = 1;
    constexpr size_t LINUX_SYS_WRITE    = 64;

    size_t strlen(const char *s) {
        size_t len = 0;
        while (s[len] != '\0') {
            ++len;
        }
        return len;
    }

    long linux_write(size_t fd, const void *buf, size_t len) {
        register size_t a0 asm("a0") = fd;
        register const void *a1 asm("a1") = buf;
        register size_t a2 asm("a2") = len;
        register size_t a7 asm("a7") = LINUX_SYS_WRITE;
        asm volatile("ecall"
                     : "+r"(a0)
                     : "r"(a1), "r"(a2), "r"(a7)
                     : "memory");
        return static_cast<long>(a0);
    }
}  // namespace

void kmod_main() {
    const char *msg = "test-posix: trigger linux write syscall\n";
    (void)linux_write(STDOUT_FD, msg, strlen(msg));
    (void)linux_write(STDOUT_FD, msg, strlen(msg));
    (void)linux_write(STDOUT_FD, msg, strlen(msg));
    while (true) {}
}
