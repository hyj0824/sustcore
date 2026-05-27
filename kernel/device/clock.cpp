/**
 * @file clock.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 时钟
 * @version alpha-1.0.0
 * @date 2026-05-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/clock.h>
#include <arch/riscv64/csr.h>

namespace device {
    /**
     * @brief 读取当前 CSR `time` 计数值.
     */
    units::tick CSRTimeClockSource::now() const noexcept {
        return csr_get_time();
    }

    /**
     * @brief 返回 CSR `time` 时钟源频率.
     */
    units::frequency CSRTimeClockSource::frequency() const noexcept {
        return _freq;
    }
}  // namespace device
