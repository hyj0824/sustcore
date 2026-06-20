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

    size_t strlen(const char *s) {
        size_t len = 0;
        while (s[len] != '\0') {
            ++len;
        }
        return len;
    }
}  // namespace

extern "C" long linux_write(size_t fd, const void *buf, size_t len);

extern "C" [[noreturn]] void test_posix_main() {
    const char *msg = "test-posix: trigger linux write syscall\n";
    (void)linux_write(STDOUT_FD, msg, strlen(msg));
    (void)linux_write(STDOUT_FD, msg, strlen(msg));
    (void)linux_write(STDOUT_FD, msg, strlen(msg));
    while (true) {
    }
}
