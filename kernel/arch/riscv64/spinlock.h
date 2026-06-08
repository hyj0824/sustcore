/**
 * @file spinlock.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 自旋锁
 * @version alpha-1.0.0
 * @date 2026-06-09
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <features/attributes.h>

#include <cstdint>

using raw_spinlock_t = volatile uint32_t;

__ATTR_ALWAYS_INLINE__
static void __raw_spin_lock(raw_spinlock_t *lock) {
    uint32_t tmp = 1;
    asm volatile(
        "1:                          \n"
        "  amoswap.w.aq %0, %0, (%1) \n"  // swap lock & tmp
        "  bnez %0, 1b               \n"  // if tmp is still 1, then the lock is
                                          // still unreleased
        : "=&r"(tmp)
        : "r"(lock)
        : "memory");
    // at this time, the lock is acquired
}

__ATTR_ALWAYS_INLINE__
static void __raw_spin_unlock(raw_spinlock_t *lock) {
    asm volatile("amoswap.w.rl zero, zero, (%0)" : : "r"(lock) : "memory");
}