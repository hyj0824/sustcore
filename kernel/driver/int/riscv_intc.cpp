/**
 * @file riscv_intc.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief RISC-V CPU 本地中断设备与驱动
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <arch/riscv64/csr.h>
#include <driver/int/riscv_intc.h>
#include <logger.h>

namespace driver {
    /**
     * @brief 创建一个 CPU 本地中断设备驱动.
     */
    Result<util::owner<RiscVIntC *>> RiscVIntC::create(
        ResPack res, intc_t identifier,
        device::cpuid_t hart_id) noexcept {
        if (res.node == nullptr) {
            loggers::INTERRUPT::ERROR("RiscVIntC 创建失败: node 为空");
            unexpect_return(ErrCode::NULLPTR);
        }

        auto *device = new RiscVIntC(std::move(res), identifier, hart_id);
        if (device == nullptr) {
            loggers::INTERRUPT::ERROR("RiscVIntC 创建失败: 内存不足");
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        loggers::INTERRUPT::DEBUG("创建 RiscVIntC 设备: id=%u hart=%u name=%s",
                                  identifier, hart_id, device->name());
        return util::owner<RiscVIntC *>(device);
    }

    /**
     * @brief 构造一个 CPU 本地中断设备驱动.
     */
    RiscVIntC::RiscVIntC(ResPack res, intc_t identifier,
                         device::cpuid_t hart_id) noexcept
        : device::IrqChip(std::move(res)),
          _identifier(identifier),
          _hart_id(hart_id) {}

    std::string_view RiscVIntC::compatible() const noexcept {
        return "riscv,cpu-intc";
    }

    /**
     * @brief 获取 MMIO 区域列表.
     */
    std::vector<PhyArea> RiscVIntC::mmio_regions() const noexcept {
        std::vector<PhyArea> regions;
        regions.reserve(mmio_resources().size());
        for (const auto &resource : mmio_resources()) {
            assert(resource != nullptr);
            regions.push_back(resource->region());
        }
        return regions;
    }

    /**
     * @brief 获取设备标识.
     */
    intc_t RiscVIntC::identifier() const noexcept {
        return _identifier;
    }

    /**
     * @brief 获取所属 hart.
     */
    device::cpuid_t RiscVIntC::hart_id() const noexcept {
        return _hart_id;
    }

    /**
     * @brief 使能本地中断.
     */
    Result<void> RiscVIntC::enable_irq(hwirq_t hw_irq) noexcept {
        loggers::INTERRUPT::DEBUG("RiscVIntC[%u] enable_irq hwirq=%u",
                                  identifier(), static_cast<unsigned>(hw_irq));
        csr_sie_t sie = csr_get_sie();
        switch (hw_irq) {
            case RiscVIntC::SOFTWARE_LOCAL_IRQ:
                sie.ssie = 1;
                csr_set_sie(sie);
                void_return();
            case RiscVIntC::CLOCK_LOCAL_IRQ:
                sie.stie = 1;
                csr_set_sie(sie);
                void_return();
            case RiscVIntC::EXTERNAL_LOCAL_IRQ:
                sie.seie = 1;
                csr_set_sie(sie);
                void_return();
            default:
                loggers::INTERRUPT::ERROR("RiscVIntC 不支持 enable hwirq=%u",
                                          static_cast<unsigned>(hw_irq));
                unexpect_return(ErrCode::NOT_SUPPORTED);
        }
    }

    /**
     * @brief 屏蔽本地中断.
     */
    Result<void> RiscVIntC::disable_irq(hwirq_t hw_irq) noexcept {
        loggers::INTERRUPT::DEBUG("RiscVIntC[%u] disable_irq hwirq=%u",
                                  identifier(), static_cast<unsigned>(hw_irq));
        csr_sie_t sie = csr_get_sie();
        switch (hw_irq) {
            case RiscVIntC::SOFTWARE_LOCAL_IRQ:
                sie.ssie = 0;
                csr_set_sie(sie);
                void_return();
            case RiscVIntC::CLOCK_LOCAL_IRQ:
                sie.stie = 0;
                csr_set_sie(sie);
                void_return();
            case RiscVIntC::EXTERNAL_LOCAL_IRQ:
                sie.seie = 0;
                csr_set_sie(sie);
                void_return();
            default:
                loggers::INTERRUPT::ERROR("RiscVIntC 不支持 disable hwirq=%u",
                                          static_cast<unsigned>(hw_irq));
                unexpect_return(ErrCode::NOT_SUPPORTED);
        }
    }

    /**
     * @brief 设置中断优先级.
     */
    Result<void> RiscVIntC::set_priority(hwirq_t hw_irq,
                                         domain_t prio) noexcept {
        (void)hw_irq;
        (void)prio;
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 设置中断亲和性.
     */
    Result<void> RiscVIntC::set_affinity(hwirq_t hw_irq,
                                         cpu_mask_t mask) noexcept {
        (void)hw_irq;
        (void)mask;
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 应答中断.
     */
    Result<void> RiscVIntC::ack_irq(hwirq_t hw_irq) noexcept {
        (void)hw_irq;
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 设置中断触发方式.
     */
    Result<void> RiscVIntC::set_trigger(hwirq_t hw_irq,
                                        device::IrqTrigger trigger) noexcept {
        (void)hw_irq;
        (void)trigger;
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }
}  // namespace driver
