/**
 * @file int.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief interrupt
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/int.h>
#include <device/model.h>
#include <driver/int/base.h>
#include <logger.h>
#include <sbi/sbi.h>

namespace driver {
    /**
     * @brief 编程下一次 CLINT 定时器事件.
     */
    void ClintAlarm::set_next_event(units::time delta) noexcept {
        units::tick now  = _clksrc->now();
        units::tick gaps = delta * _clksrc->frequency();
        sbi_legacy_set_timer(now + gaps);
    }

    /**
     * @brief 构造绑定到指定时钟源的 CLINT 定时事件设备.
     */
    ClintAlarm::ClintAlarm(ClockSource *clksrc, virq_t clock_virq) noexcept
        : Alarm(clksrc), _clock_virq(clock_virq) {
        _last_recorded_time = _clksrc->to_ns(_clksrc->now());
        assert(device::DeviceModel::initialized());
        auto &irqman      = device::DeviceModel::inst().interrupt();
        auto register_res = irqman.register_handler(
            clock_virq, this_call(this, &ClintAlarm::handle_irq));
        assert(register_res.has_value());
    }

    /**
     * @brief 作为中断系统 handler 处理 clock_virq.
     */
    void ClintAlarm::handle_irq(const IrqEvent &event) noexcept {
        if (event.virq != _clock_virq) {
            loggers::INTERRUPT::ERROR(
                "ClintAlarm 收到不匹配的 virq: got=%llu expect=%llu",
                static_cast<unsigned long long>(event.virq),
                static_cast<unsigned long long>(_clock_virq));
            return;
        }
        units::time now = _clksrc->to_ns(_clksrc->now());
        if (_handler) {
            _handler(ClockEvent{.last = _last_recorded_time, .now = now});
        }
        _last_recorded_time = now;
        auto ack_res = device::DeviceModel::inst().interrupt().ack(event);
        if (!ack_res.has_value()) {
            loggers::INTERRUPT::ERROR("ClintAlarm 处理 IRQ 时 ack 失败: %s",
                                       to_cstring(ack_res.error()));
        }
    }
}  // namespace driver
