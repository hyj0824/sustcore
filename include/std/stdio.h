/**
 * @file stdio.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief stdio.h
 * @version alpha-1.0.0
 * @date 2023-08-21
 *
 * @copyright Copyright (c) 2022 TayhuangOS Development Team
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#pragma once

#ifdef __cplusplus
#define restrict __restrict__
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>

typedef int (*printf_write_fn)(const char *data, size_t len, void *ctx);

/**
 * @brief 输出到buffer中
 *
 * @param buffer 缓存
 * @param fmt 格式化字符串
 * @param args 参数
 * @return 输出字符数
 */
int vsprintf(char *buffer, const char *fmt, va_list args);

/**
 * @brief 输出到buffer中, 最多输出buf_size-1个字符, 并在末尾添加'\0'
 * 
 * @param buffer 缓存
 * @param buf_size 缓存大小
 * @param fmt 格式化字符串
 * @param args 参数
 * @return int 完整输出字符数, 不包括末尾的'\0'
 */
int vsnprintf(char *buffer, size_t buf_size, const char *fmt, va_list args);

/**
 * @brief 分块格式化输出
 *
 * @param chunk 临时块缓冲
 * @param chunk_size 临时块缓冲大小
 * @param write 块输出函数
 * @param ctx 输出函数上下文
 * @param fmt 格式化字符串
 * @param args 参数
 * @return int 完整输出字符数, 不包括末尾的'\0'
 */
int vcbprintf(char *chunk, size_t chunk_size, printf_write_fn write,
              void *ctx, const char *fmt, va_list args);

/**
 * @brief 输出到buffer中
 *
 * @param buffer 缓存
 * @param fmt 格式化字符串
 * @param ... 参数
 * @return 输出字符数
 */
int sprintf(char *buffer, const char *fmt, ...);

/**
 * @brief 输出到buffer中, 最多输出buf_size-1个字符, 并在末尾添加'\0'
 * 
 * @param buffer 缓存
 * @param buf_size 缓存大小
 * @param fmt 格式化字符串
 * @param ... 参数
 * @return int 完整输出字符数, 不包括末尾的'\0'
 */
int snprintf(char *buffer, size_t buf_size, const char *fmt, ...);

/**
 * @brief 格式化输出到标准输出
 * 
 * @param fmt 格式化字符串
 * @param ... 可变参数
 * @return int 输出的字符数
 */
int printf(const char *fmt, ...);

/**
 * @brief 输出字符串到标准输出
 * 
 * @param str 要输出的字符串
 * @return int 输出的字符数
 */
int puts(const char *str);

#ifdef __cplusplus
}
#undef restrict
#endif
