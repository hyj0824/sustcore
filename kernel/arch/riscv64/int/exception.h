/**
 * @file exception.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 异常处理程序头文件
 * @version alpha-1.0.0
 * @date 2025-11-18
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#pragma once

#include <sus/bits.h>

typedef void (*ISRService)(void);

#define IVT_ENTRIES (16)

extern dword IVT[IVT_ENTRIES];

void init_ivt();