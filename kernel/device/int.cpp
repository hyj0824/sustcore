/**
 * @file int.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief interrupt
 * @version alpha-1.0.0
 * @date 2026-05-26
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <arch/riscv64/csr.h>
#include <device/int.h>
#include <sbi/sbi.h>

namespace device {
    /**
     * @brief 编程下一次 CLINT 定时器事件.
     */
    void ClintTimer::setNextEvent(units::time delta) noexcept {
        // 获取当前 mtime，计算绝对计数值，调用 SBI
        units::tick now  = _clksrc->now();
        units::tick gaps = delta * _clksrc->frequency();
        sbi_legacy_set_timer(now + gaps);
    }

    /**
     * @brief 处理一次 CLINT 定时器中断并触发回调.
     */
    void ClintTimer::onTimerIrq() noexcept
    {
        units::time now = _clksrc->to_ns(_clksrc->now());
        if (_handler) {
            _handler({ .last = _last_recorded_time, .now = now });
        }
        _last_recorded_time = now;
    }

    /**
     * @brief 构造一个 CLINT 控制器对象.
     */
    Clint::Clint(std::string name, ictrl_t identifier,
                 std::vector<PhyArea> mmio_regions, cpuid_t hart_id) noexcept
        : _name(std::move(name)),
          _identifier(identifier),
          _mmio_regions(std::move(mmio_regions)),
          _hart_id(hart_id) {}

    /**
     * @brief 获取控制器名称.
     */
    const char *Clint::name() const noexcept {
        return _name.c_str();
    }

    /**
     * @brief 获取 MMIO 区域列表.
     */
    std::vector<PhyArea> Clint::mmio_regions() const {
        return _mmio_regions;
    }

    /**
     * @brief 使能本地中断.
     */
    Result<void> Clint::enable_irq(hwirq_t hw_irq) {
        loggers::INTERRUPT::DEBUG("Clint[%u] enable_irq hwirq=%u", _identifier,
                                  hw_irq);

        // 根据 IRQ 编号打开对应的 S 态本地中断位.
        csr_sie_t sie = csr_get_sie();
        switch (hw_irq) {
            case S_SOFT_IRQ:
                sie.ssie = 1;
                csr_set_sie(sie);
                void_return();
            case S_TIMER_IRQ:
                sie.stie = 1;
                csr_set_sie(sie);
                void_return();
            default:
                loggers::INTERRUPT::ERROR("Clint[%u] 不支持启用 hwirq=%u",
                                          _identifier, hw_irq);
                unexpect_return(ErrCode::NOT_SUPPORTED);
        }
    }

    /**
     * @brief 屏蔽本地中断.
     */
    Result<void> Clint::disable_irq(hwirq_t hw_irq) {
        loggers::INTERRUPT::DEBUG("Clint[%u] disable_irq hwirq=%u", _identifier,
                                  hw_irq);

        // 根据 IRQ 编号关闭对应的 S 态本地中断位.
        csr_sie_t sie = csr_get_sie();
        switch (hw_irq) {
            case S_SOFT_IRQ: {
                sie.ssie = 0;
                csr_set_sie(sie);
                [[maybe_unused]] SBIRet ret = sbi_legacy_clear_ipi();
                void_return();
            }
            case S_TIMER_IRQ:
                sie.stie = 0;
                csr_set_sie(sie);
                void_return();
            default:
                loggers::INTERRUPT::ERROR("Clint[%u] 不支持关闭 hwirq=%u",
                                          _identifier, hw_irq);
                unexpect_return(ErrCode::NOT_SUPPORTED);
        }
    }

    /**
     * @brief 设置中断优先级.
     */
    Result<void> Clint::set_priority(hwirq_t hw_irq, irq_prio_t prio) {
        loggers::INTERRUPT::DEBUG(
            "Clint[%u] set_priority hwirq=%u prio=%u 不支持", _identifier,
            hw_irq, prio);
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 设置中断亲和性.
     */
    Result<void> Clint::set_affinity(hwirq_t hw_irq, cpu_mask_t mask) {
        loggers::INTERRUPT::ERROR(
            "Clint[%u] set_affinity hwirq=%u mask=0x%llx 不支持", _identifier,
            hw_irq, static_cast<unsigned long long>(mask));
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }
}  // namespace device
