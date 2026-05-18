/**
 * @file logger.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief logger 头文件
 * @version alpha-1.0.0
 * @date 2023-4-3
 *
 * @copyright Copyright (c) 2022 TayhuangOS Development Team
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#pragma once

#include <sus/ansi.h>
#include <concepts>
#include <cstdio>
#include <cstddef>

enum class LogLevel { DEBUG = 0, INFO, WARN, ERROR, FATAL, DISABLE };

static constexpr LogLevel GLOBAL_LOG_LEVEL = LogLevel::DEBUG;

constexpr const char *level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:
            return ANSI_GRAPHIC(ANSI_FG_BLUE) "DEBUG" ANSI_GRAPHIC(
                ANSI_GM_RESET);
        case LogLevel::INFO:
            return ANSI_GRAPHIC(ANSI_FG_GREEN) "INFO" ANSI_GRAPHIC(
                ANSI_GM_RESET);
        case LogLevel::WARN:
            return ANSI_GRAPHIC(ANSI_FG_MAGENTA) "WARN" ANSI_GRAPHIC(
                ANSI_GM_RESET);
        case LogLevel::ERROR:
            return ANSI_GRAPHIC(ANSI_FG_RED) "ERROR" ANSI_GRAPHIC(
                ANSI_GM_RESET);
        case LogLevel::FATAL:
            return ANSI_GRAPHIC(ANSI_FG_RED) "FATAL" ANSI_GRAPHIC(
                ANSI_GM_RESET);
        default: return "UNKNOWN";
    }
}

namespace basecpp {
    template <typename T>
    concept LogInfo = requires() {
        {
            T::name
        } -> std::convertible_to<const char *>;
        {
            T::level
        } -> std::same_as<const LogLevel &>;
    };

    template <typename... Args>
    void append_log(char *buffer, size_t buffer_size, size_t &offset,
                    const char *fmt, Args... args) {
        if (buffer_size == 0 || offset >= buffer_size) {
            return;
        }

        int len = snprintf(buffer + offset, buffer_size - offset, fmt, args...);
        if (len < 0) {
            buffer[buffer_size - 1] = '\0';
            return;
        }

        size_t written = static_cast<size_t>(len);
        if (written >= buffer_size - offset) {
            offset = buffer_size - 1;
        } else {
            offset += written;
        }
    }
}  // namespace basecpp

template <basecpp::LogInfo LogInfo, typename PutFunctor>
class Logger {
private:
    template <LogLevel level, typename... Args>
    static void __log__(const char *file, const int line, const char *func,
                        const char *fmt, Args... args);

    template <LogLevel level, typename... Args>
    static void __log__(const char *fmt, Args... args);
    inline static PutFunctor __put{};
public:
    template <typename... Args>
    static void debug(const char *file, const int line, const char *func,
                      const char *fmt, Args... args);

    template <typename... Args>
    static void info(const char *file, const int line, const char *func,
                     const char *fmt, Args... args);

    template <typename... Args>
    static void warn(const char *file, const int line, const char *func,
                     const char *fmt, Args... args);

    template <typename... Args>
    static void error(const char *file, const int line, const char *func,
                      const char *fmt, Args... args);

    template <typename... Args>
    static void fatal(const char *file, const int line, const char *func,
                      const char *fmt, Args... args);
};

template <basecpp::LogInfo LogInfo, typename PutFunctor>
template <LogLevel level, typename... Args>
void Logger<LogInfo, PutFunctor>::__log__(const char *file, const int line,
                                         const char *func, const char *fmt,
                                         Args... args) {
    if constexpr (level < GLOBAL_LOG_LEVEL) {
        return;
    }

    if constexpr (level < LogInfo::level) {
        return;
    }

    char buffer[256];
    size_t offset = 0;

    basecpp::append_log(buffer, sizeof(buffer), offset, "(%s:%d:%s)", file,
                        line, func);
    basecpp::append_log(buffer, sizeof(buffer), offset, "[%s:%s]: ",
                        LogInfo::name, level_to_string(level));

    basecpp::append_log(buffer, sizeof(buffer), offset, fmt, args...);
    basecpp::append_log(buffer, sizeof(buffer), offset, "\n");
    __put(buffer);
}

template <basecpp::LogInfo LogInfo, typename PutFunctor>
template <LogLevel level, typename... Args>
void Logger<LogInfo, PutFunctor>::__log__(const char *fmt, Args... args) {
    if constexpr (level < GLOBAL_LOG_LEVEL) {
        return;
    }

    if constexpr (level < LogInfo::level) {
        return;
    }

    char buffer[256];
    size_t offset = 0;

    basecpp::append_log(buffer, sizeof(buffer), offset, "[%s:%s]: ",
                        LogInfo::name, level_to_string(level));

    basecpp::append_log(buffer, sizeof(buffer), offset, fmt, args...);
    basecpp::append_log(buffer, sizeof(buffer), offset, "\n");
    __put(buffer);
}

template <basecpp::LogInfo LogInfo, typename PutFunctor>
template <typename... Args>
void Logger<LogInfo, PutFunctor>::debug(const char *file, const int line,
                                       const char *func, const char *fmt,
                                       Args... args) {
    __log__<LogLevel::DEBUG>(file, line, func, fmt, args...);
}

#define DEBUG(fmt, ...) debug(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

template <basecpp::LogInfo LogInfo, typename PutFunctor>
template <typename... Args>
void Logger<LogInfo, PutFunctor>::info(const char *file, const int line,
                                      const char *func, const char *fmt,
                                      Args... args) {
    __log__<LogLevel::INFO>(file, line, func, fmt, args...);
}

#define INFO(fmt, ...) info(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

template <basecpp::LogInfo LogInfo, typename PutFunctor>
template <typename... Args>
void Logger<LogInfo, PutFunctor>::warn(const char *file, const int line,
                                      const char *func, const char *fmt,
                                      Args... args) {
    __log__<LogLevel::WARN>(file, line, func, fmt, args...);
}

#define WARN(fmt, ...) warn(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

template <basecpp::LogInfo LogInfo, typename PutFunctor>
template <typename... Args>
void Logger<LogInfo, PutFunctor>::error(const char *file, const int line,
                                       const char *func, const char *fmt,
                                       Args... args) {
    __log__<LogLevel::ERROR>(file, line, func, fmt, args...);
}

#define ERROR(fmt, ...) error(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

template <basecpp::LogInfo LogInfo, typename PutFunctor>
template <typename... Args>
void Logger<LogInfo, PutFunctor>::fatal(const char *file, const int line,
                                       const char *func, const char *fmt,
                                       Args... args) {
    __log__<LogLevel::FATAL>(file, line, func, fmt, args...);
}

#define FATAL(fmt, ...) fatal(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define DECLARE_LOGGER(PutFunctor, _logLevel, loggerName)       \
    struct loggerName##Logger {                                \
        static constexpr const char name[] = #loggerName;      \
        static constexpr LogLevel level    = _logLevel;        \
    };                                                         \
    static_assert(basecpp::LogInfo<loggerName##Logger>,        \
                  #loggerName " Logger static assert failed"); \
    using loggerName = Logger<loggerName##Logger, PutFunctor>;
