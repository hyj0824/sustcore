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
    IntCtrlManager IntCtrlManager::_INSTANCE;
    bool IntCtrlManager::_initialized = false;

    /**
     * @brief 构造一个 CLINT 控制器对象.
     */
    Clint::Clint(std::string name, ictrl_t identifier,
                 std::vector<PhyArea> mmio_regions,
                 std::vector<b32> target_harts) noexcept
        : _name(std::move(name)),
          _identifier(identifier),
          _mmio_regions(std::move(mmio_regions)),
          _target_harts(std::move(target_harts)),
          _hart_mask(0) {
        for (b32 hart : _target_harts) {
            if (hart < sizeof(cpu_mask_t) * 8U) {
                _hart_mask |= static_cast<cpu_mask_t>(1ULL << hart);
            }
        }
    }

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
