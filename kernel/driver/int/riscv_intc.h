/**
 * @file riscv_intc.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief RISC-V CPU 本地中断设备与驱动
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <device/model.h>

#include <string>

namespace driver {
    /**
     * @brief RISC-V CPU 本地中断驱动.
     */
    class RiscVIntC final : public IrqChip {
    public:
        static constexpr hwirq_t SOFTWARE_LOCAL_IRQ_S  = 1;
        static constexpr hwirq_t SOFTWARE_LOCAL_IRQ    = 3;
        static constexpr hwirq_t CLOCK_LOCAL_IRQ_S     = 5;
        static constexpr hwirq_t CLOCK_LOCAL_IRQ       = 7;
        static constexpr hwirq_t EXTERNAL_LOCAL_IRQ    = 9;
        static constexpr const char *COMPATIBLE_STRING = "riscv,local-intc";

        /**
         * @brief 创建一个 CPU 本地中断驱动.
         *
         * @param res 统一设备节点与资源集合.
         * @param identifier 设备标识.
         * @param hart_id 所属 hart.
         * @return Result<util::owner<RiscVIntC*>> 创建结果.
         */
        [[nodiscard]]
        static Result<util::owner<RiscVIntC *>> create(
            DevRes res, intc_t identifier, device::cpuid_t hart_id) noexcept;

        /**
         * @brief 销毁设备驱动.
         */
        ~RiscVIntC() override = default;

        [[nodiscard]]
        std::string_view compatible() const noexcept override;

        /**
         * @brief 获取 MMIO 区域列表.
         *
         * @return std::vector<PhyArea> MMIO 区域列表.
         */
        [[nodiscard]]
        std::vector<PhyArea> mmio_regions() const noexcept;

        /**
         * @brief 获取设备标识.
         *
         * @return intc_t 设备标识.
         */
        [[nodiscard]]
        intc_t identifier() const noexcept;

        /**
         * @brief 获取所属 hart.
         *
         * @return device::cpuid_t hart ID.
         */
        [[nodiscard]]
        device::cpuid_t hart_id() const noexcept;
        [[nodiscard]]
        Result<void> enable_irq(hwirq_t hw_irq) noexcept override;
        [[nodiscard]]
        Result<void> disable_irq(hwirq_t hw_irq) noexcept override;
        [[nodiscard]]
        Result<void> set_priority(hwirq_t hw_irq,
                                  domain_t prio) noexcept override;
        [[nodiscard]]
        Result<void> set_affinity(hwirq_t hw_irq,
                                  cpu_mask_t mask) noexcept override;
        [[nodiscard]]
        Result<void> ack(const IrqEvent &event) noexcept override;
        [[nodiscard]]
        Result<void> set_trigger(hwirq_t hw_irq,
                                 IrqTrigger trigger) noexcept override;

        [[nodiscard]]
        Result<void> post(hwirq_t hw_irq) noexcept {
            auto virq_res = _domain->to_virq(hw_irq);
            propagate(virq_res);

            auto res = disable_irq(hw_irq);
            propagate(res);

            return irqman().dispatch(IrqEvent{.virq          = virq_res.value(),
                                              .hw_irq        = hw_irq,
                                              .domain        = _domain,
                                              .chip_specific = {}});
        }

    private:
        IrqManager &irqman() {
            return device::DeviceModel::inst().interrupt();
        }

        /**
         * @brief 构造一个 CPU 本地中断设备驱动.
         *
         * @param res 统一 CPU 本地中断设备节点与资源集合.
         * @param identifier 设备标识.
         * @param hart_id 所属 hart.
         * @param _domain 中断域.
         */
        RiscVIntC(DevRes res, intc_t identifier,
                  device::cpuid_t hart_id) noexcept;

        // 初始化 RiscVIntC
        [[nodiscard]]
        Result<void> initialize(IrqDomain *domain);

        IrqDomain *_domain       = nullptr;
        intc_t _identifier       = device::INVALID_ICTRL_ID;
        device::cpuid_t _hart_id = 0;
    };
}  // namespace driver
