/**
 * @file baseio.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief primitive io - 实现
 * @version alpha-1.0.0
 * @date 2023-04-08
 *
 * @copyright Copyright (c) 2022 TayhuangOS Development Team
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include <sus/logger.h>
#include <sus/tostring.h>
#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstring>

namespace {
    constexpr size_t unlimited_size = static_cast<size_t>(-1);
    constexpr size_t int_max_value =
        static_cast<size_t>(static_cast<unsigned int>(-1) >> 1);

    struct BoundedWriter {
        char *buffer;
        size_t size;
        size_t written;
        bool failed;

        void put(char ch) {
            if (buffer != nullptr && size > 0 && written < size - 1) {
                buffer[written] = ch;
            }
            written++;
        }

        void write(const char *str, size_t len) {
            for (size_t i = 0; i < len; i++) {
                put(str[i]);
            }
        }

        void finish() {
            if (buffer == nullptr || size == 0) {
                return;
            }
            size_t end = written < size ? written : size - 1;
            buffer[end] = '\0';
        }
    };

    struct ChunkWriter {
        char *chunk;
        size_t chunk_size;
        printf_write_fn write_fn;
        void *ctx;
        size_t offset;
        size_t written;
        bool failed;

        void flush() {
            if (failed || offset == 0) {
                return;
            }
            if (write_fn(chunk, offset, ctx) < 0) {
                failed = true;
            }
            offset = 0;
        }

        void put(char ch) {
            if (failed) {
                return;
            }
            if (chunk == nullptr || chunk_size == 0 || write_fn == nullptr) {
                failed = true;
                return;
            }
            if (offset == chunk_size) {
                flush();
                if (failed) {
                    return;
                }
            }
            chunk[offset++] = ch;
            written++;
        }

        void write(const char *str, size_t len) {
            for (size_t i = 0; i < len; i++) {
                put(str[i]);
            }
        }

        void finish() {
            flush();
        }
    };

    size_t strn_len(const char *str, int precision) {
        if (str == nullptr) {
            str = "(null)";
        }

        size_t len = 0;
        while (str[len] != '\0' &&
               (precision < 0 || len < static_cast<size_t>(precision)))
        {
            len++;
        }
        return len;
    }

    template <typename Writer>
    void write_repeat(Writer &writer, char ch, size_t count) {
        for (size_t i = 0; i < count; i++) {
            writer.put(ch);
        }
    }

    template <typename Writer>
    int format_output(Writer &writer, const char *fmt, va_list args) {
    enum {
        // 标志
        FLAG_SIGN         = 1 << 0,  // 显示符号
        FLAG_LEFT_ALIGN   = 1 << 1,  // 左对齐
        FLAG_FILL_ZERO    = 1 << 2,  // 用0填充
        FLAG_PREFIX       = 1 << 3,  // 前缀0x/0
        FLAG_UPPER        = 1 << 4,  // 大写十六进制
        // 打印类型
        PRINT_TY_INT      = 0,  // 整型
        PRINT_TY_UNSIGNED = 1,  // 无符号整型
        PRINT_TY_OCT      = 2,  // 八进制
        PRINT_TY_HEX      = 3,  // 十六进制
        PRINT_TY_CHAR     = 4,  // 字符
        PRINT_TY_STRING   = 5,  // 字符串
        // 位长
        QUAL_SHORT        = 0,  // 短
        QUAL_NORMAL       = 1,  // 普通
        QUAL_LONG         = 2,  // 长
        QUAL_LONG_LONG    = 3   // 长长
    };

    // fmt未结束
    while (*fmt) {
        // 非格式化字符
        if (*fmt != '%') {
            // 直接加入
            writer.put(*fmt);
            fmt++;
        } else {
            // 格式化
            fmt++;

            // %%
            if (*fmt == '%') {
                writer.put('%');
                fmt++;
                continue;
            }

            // 格式化信息
            unsigned char flag       = 0;
            int width                = 0;
            int precision            = -1;
            unsigned char print_type = PRINT_TY_INT;
            unsigned char qualifier  = QUAL_NORMAL;

            // 特殊标记(+ - 0 #)
            while (true) {
                bool _break = false;
                switch (*fmt) {
                    case '+':
                        flag |= FLAG_SIGN;
                        fmt++;
                        break;
                    case '-':
                        flag |= FLAG_LEFT_ALIGN;
                        fmt++;
                        break;
                    case ' ': fmt++; break;
                    case '0':
                        flag |= FLAG_FILL_ZERO;
                        fmt++;
                        break;
                    case '#':
                        flag |= FLAG_PREFIX;
                        fmt++;
                        break;
                    default: _break = true; break;  // 标记结束
                }
                if (_break) {
                    break;
                }
            }

            // 对齐标记
            // *说明放在可变参数表中
            if (*fmt == '*') {
                width = va_arg(args, int);
                // 负数说明左对齐
                if (width < 0) {
                    width  = -width;
                    flag  |= FLAG_LEFT_ALIGN;
                }
                fmt++;
            } else {
                // 获取对齐宽度
                while (isdigit(static_cast<unsigned char>(*fmt))) {
                    width = width * 10 + *fmt - '0';
                    fmt++;
                }
            }

            // 精度标记
            // *说明放在可变参数表中
            if (*fmt == '.') {
                fmt++;
                if (*fmt == '*') {
                    precision = va_arg(args, int);
                    if (precision < 0) {
                        precision = -1;
                    }
                    fmt++;
                } else {
                    precision = 0;
                    // 获取精度
                    while (isdigit(static_cast<unsigned char>(*fmt))) {
                        precision = precision * 10 + *fmt - '0';
                        fmt++;
                    }
                }
            }

            // 精度拓展标记
            switch (*fmt) {
                case 'l':
                case 'L':
                    qualifier = QUAL_LONG;
                    fmt++;
                    if (*fmt == 'l' || *fmt == 'L') {
                        qualifier = QUAL_LONG_LONG;
                        fmt++;
                    }
                    break;
                case 'h':
                    qualifier = QUAL_SHORT;
                    fmt++;
                    break;
            }

            // 类型标记
            char specifier = *fmt;
            switch (specifier) {
                case 'd': print_type = PRINT_TY_INT; break;
                case 'u': print_type = PRINT_TY_UNSIGNED; break;
                case 'o': print_type = PRINT_TY_OCT; break;
                case 'X': flag |= FLAG_UPPER;  // 大写十六进制
                case 'x': print_type = PRINT_TY_HEX; break;
                case 'c': print_type = PRINT_TY_CHAR; break;
                case 's': print_type = PRINT_TY_STRING; break;
                case 'P': flag |= FLAG_UPPER;  // 大写指针
                case 'p':
                    flag       |= FLAG_FILL_ZERO;
                    flag       |= FLAG_PREFIX;
                    width       = 18;
                    print_type  = PRINT_TY_HEX;
                    break;  // 指针
                default: print_type = -1; break;
            }

            // 解析失败
            if (print_type == -1) {
                return -1;
            }
            fmt++;

            // 符号标记和缓存
            char signch       = '\0';
            const char *prefix = "";
            size_t prefix_len = 0;
            char _buffer[120] = {};
            const char *data  = _buffer;
            size_t data_len   = 0;

            switch (print_type) {
                // 整型
                case PRINT_TY_INT: {
                    long long val = 0;
                    if (qualifier == QUAL_LONG_LONG) {
                        val = va_arg(args, long long);
                    } else if (qualifier == QUAL_LONG) {
                        val = va_arg(args, long);
                    } else {
                        val = va_arg(args, int);
                    }

                    unsigned long long abs_val = 0;
                    if (val < 0) {
                        signch  = '-';
                        abs_val = static_cast<unsigned long long>(-(val + 1)) + 1;
                    } else {
                        if (flag & FLAG_SIGN) {
                            signch = '+';
                        }
                        abs_val = static_cast<unsigned long long>(val);
                    }
                    ulltoa(abs_val, _buffer, 10);
                    break;
                }
                // 无符号整型
                case PRINT_TY_UNSIGNED: {
                    unsigned long long val = 0;
                    if (qualifier == QUAL_LONG_LONG) {
                        val = va_arg(args, unsigned long long);
                    } else if (qualifier == QUAL_LONG) {
                        val = va_arg(args, unsigned long);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    ulltoa(val, _buffer, 10);
                    break;
                }
                // 八进制
                case PRINT_TY_OCT: {
                    unsigned long long val = 0;
                    if (qualifier == QUAL_LONG_LONG) {
                        val = va_arg(args, unsigned long long);
                    } else if (qualifier == QUAL_LONG) {
                        val = va_arg(args, unsigned long);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    ulltoa(val, _buffer, 8);
                    if ((flag & FLAG_PREFIX) && _buffer[0] != '0') {
                        prefix = "0";
                    }
                    break;
                }
                // 十六进制
                case PRINT_TY_HEX: {
                    unsigned long long val = 0;
                    if (specifier == 'p' || specifier == 'P') {
                        val = reinterpret_cast<unsigned long long>(
                            va_arg(args, void *));
                    } else if (qualifier == QUAL_LONG_LONG) {
                        val = va_arg(args, unsigned long long);
                    } else if (qualifier == QUAL_LONG) {
                        val = va_arg(args, unsigned long);
                    } else {
                        val = va_arg(args, unsigned int);
                    }
                    ulltoa(val, _buffer, 16);

                    // 若大写
                    if (flag & FLAG_UPPER) {
                        char *__buffer = _buffer;
                        while (*__buffer != '\0') {
                            if (islower(static_cast<unsigned char>(*__buffer))) {
                                *__buffer = toupper(*__buffer);
                            }
                            __buffer++;
                        }
                    }
                    if (flag & FLAG_PREFIX) {
                        prefix = flag & FLAG_UPPER ? "0X" : "0x";
                    }
                    break;
                }
                // 字符
                case PRINT_TY_CHAR: {
                    char ch = (char)va_arg(args, unsigned int);
                    _buffer[0] = ch;
                    _buffer[1] = '\0';
                    break;
                }
                // 字符串
                case PRINT_TY_STRING: {
                    char *str = va_arg(args, char *);
                    data = str != nullptr ? str : "(null)";
                    break;
                }
            }

            if (print_type == PRINT_TY_STRING) {
                data_len = strn_len(data, precision);
            } else if (print_type == PRINT_TY_CHAR) {
                data_len = 1;
            } else {
                data_len = strlen(data);
                if (precision == 0 && data_len == 1 && data[0] == '0') {
                    data_len = 0;
                }
            }

            prefix_len = strlen(prefix);
            size_t sign_len = signch != '\0' ? 1 : 0;
            size_t precision_zeroes = 0;
            if (print_type != PRINT_TY_STRING && print_type != PRINT_TY_CHAR &&
                precision > 0 && static_cast<size_t>(precision) > data_len)
            {
                precision_zeroes = static_cast<size_t>(precision) - data_len;
            }

            bool zero_pad = (flag & FLAG_FILL_ZERO) && !(flag & FLAG_LEFT_ALIGN) &&
                            precision < 0;
            size_t raw_len = sign_len + prefix_len + precision_zeroes + data_len;
            size_t padding = width > 0 && static_cast<size_t>(width) > raw_len
                                 ? static_cast<size_t>(width) - raw_len
                                 : 0;

            if (!(flag & FLAG_LEFT_ALIGN) && !zero_pad) {
                write_repeat(writer, ' ', padding);
            }
            if (signch != '\0') {
                writer.put(signch);
            }
            writer.write(prefix, prefix_len);
            if (!(flag & FLAG_LEFT_ALIGN) && zero_pad) {
                write_repeat(writer, '0', padding);
            }
            write_repeat(writer, '0', precision_zeroes);
            writer.write(data, data_len);
            if (flag & FLAG_LEFT_ALIGN) {
                write_repeat(writer, ' ', padding);
            }
        }
    }

    // 结束标记
    writer.finish();

    if (writer.failed || writer.written > int_max_value) {
        return -1;
    }
    return static_cast<int>(writer.written);
}

}  // namespace

/**
 * @brief 底层printf, 输出到buffer中
 *
 * @param buffer 输出缓冲
 * @param fmt 格式化字符串
 * @param args 参数
 * @return 输出字符数
 */
int vsnprintf(char *buffer, size_t buf_size, const char *fmt, va_list args) {
    BoundedWriter writer{buffer, buf_size, 0, false};
    return format_output(writer, fmt, args);
}

int vcbprintf(char *chunk, size_t chunk_size, printf_write_fn write,
              void *ctx, const char *fmt, va_list args) {
    ChunkWriter writer{chunk, chunk_size, write, ctx, 0, 0, false};
    return format_output(writer, fmt, args);
}

/**
 * @brief 底层printf, 输出到buffer中
 *
 * @param buffer 输出缓冲
 * @param fmt 格式化字符串
 * @param args 参数
 * @return 输出字符数
 */
int vsprintf(char *buffer, const char *fmt, va_list args) {
    return vsnprintf(buffer, unlimited_size, fmt, args);
}

/**
 * @brief 输出到buffer中
 *
 * @param buffer 缓存
 * @param fmt 格式化字符串
 * @param ... 参数
 * @return 输出字符数
 */
int sprintf(char *buffer, const char *fmt, ...) {
    // 初始化可变参数
    va_list lst;
    va_start(lst, fmt);

    int ret = vsprintf(buffer, fmt, lst);

    va_end(lst);
    return ret;
}

/**
 * @brief 输出到buffer中, 最多输出buf_size-1个字符, 并在末尾添加'\0'
 *
 * @param buffer 缓存
 * @param buf_size 缓存大小
 * @param fmt 格式化字符串
 * @param ... 参数
 * @return 完整输出字符数, 不包括末尾的'\0'
 */
int snprintf(char *buffer, size_t buf_size, const char *fmt, ...) {
    // 初始化可变参数
    va_list lst;
    va_start(lst, fmt);

    int ret = vsnprintf(buffer, buf_size, fmt, lst);

    va_end(lst);
    return ret;
}
