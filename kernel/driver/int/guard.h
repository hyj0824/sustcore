/**
 * @file guard.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief Interrupt guard
 * @version alpha-1.0.0
 * @date 2026-06-28
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <arch/description.h>

class InterruptGuard {
private:
    bool entered      = false;
    bool prev_enabled = false;

public:
    InterruptGuard() = default;

    /**
     * @brief 进入中断关闭保护区.
     */
    void enter() {
        if (entered) {
            return;
        }
        prev_enabled = Interrupt::enabled();
        Interrupt::cli();
        entered = true;
    }

    /**
     * @brief 退出保护区并恢复中断状态.
     */
    ~InterruptGuard() {
        if (entered && prev_enabled) {
            Interrupt::sti();
        }
    }
};