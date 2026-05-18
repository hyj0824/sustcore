/**
 * @file printf.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief printf function
 * @version alpha-1.0.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <kmod/syscall.h>
#include <prm.h>
#include <cstdio>
#include <cstring>

namespace {
    int serial_write_chunk(const char *data, size_t len, void *) {
        sys_write_serial(data, len);
        return len;
    }
}  // namespace

extern "C" {
int kputs(const char *str) {
    size_t len = strlen(str);
    sys_write_serial(str, len);
    return len;
}

int printf(const char *format, ...) {
    char chunk[256];
    va_list args;
    va_start(args, format);
    int ret = vcbprintf(chunk, sizeof(chunk), serial_write_chunk, nullptr,
                        format, args);
    va_end(args);
    return ret;
}
}
