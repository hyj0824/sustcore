/**
 * @file assert.cpp
 * @author jeromeyao (yaoshengqi726@outlook.com)
 * theflysong(song_of_the_fly@163.com)
 * @brief C Assertion Failure Handler
 * @version alpha-1.0.0
 * @date 2026-02-13
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <logger.h>

#include <cstdarg>
#include <cstdio>

namespace {
    int panic_write_chunk(const char *data, size_t len, void *) {
        sys_write_serial(data, len);
        return len;
    }

    int vpanic_print(const char *format, va_list args) {
        char chunk[256];
        return vcbprintf(chunk, sizeof(chunk), panic_write_chunk, nullptr,
                         format, args);
    }
}  // namespace

extern "C" void assertion_failure(const char *expression, const char *file,
                                  const char *base_file, int line) {
    loggers::SUSTCORE::ERROR("assertion failed: %s (%s:%d, base: %s)\n", expression, file,
                  line, base_file);
    while (true);
}

extern "C" void panic_failure(const char *expression, const char *file,
                              const char *base_file, int line) {
    loggers::SUSTCORE::ERROR("panic_assert failed: %s (%s:%d, base: %s)\n", expression,
                  file, line, base_file);
    while (true);
}

extern "C" void panic(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vpanic_print(format, args);
    va_end(args);
    kputs("\n");
    while (true);
}
