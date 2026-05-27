/**
 * @file misc.c
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 杂项
 * @version alpha-1.0.0
 * @date 2025-11-21
 *
 * @copyright Copyright (c) 2025
 *
 */

#include <arch/riscv64/csr.h>
#include <arch/riscv64/device/fdt_helper.h>
#include <arch/riscv64/device/misc.h>
#include <device/model.h>
#include <logger.h>
#include <sbi/sbi.h>
#include <sus/logger.h>
#include <sus/units.h>

TimerInfo timer_info;

units::frequency get_clock_freq(void) {
    // 读取 /cpus/timebase-frequency 属性
    return device::DeviceModel::inst().cpus().freq;
}

void init_timer(units::frequency freq, units::frequency expected_freq) {
    units::tick increment    = 1 * (freq.to_hz() / expected_freq.to_hz());
    timer_info.freq          = freq;
    timer_info.expected_freq = expected_freq;
    timer_info.increment     = increment;

    // 之后稳定触发
    sbi_legacy_set_timer(csr_get_time() + increment);

    // 启用S-Mode计时器中断
    csr_sie_t sie = csr_get_sie();
    sie.stie      = 1;
    csr_set_sie(sie);
}