/**
 * @file shutdown.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief shutdown
 * @version alpha-1.0.0
 * @date 2026-06-23
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

namespace syscall {
    [[noreturn]]
    void sys_shutdown() noexcept;

    [[noreturn]]
    void sys_block_forever() noexcept;
}  // namespace syscall
