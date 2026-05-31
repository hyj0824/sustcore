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

#include <device/int.h>

#include <string>

namespace driver {
    /**
     * @brief RISC-V CPU 本地中断驱动.
     */
    class RiscVIntC final : public device::IrqChip {
    public:
        static constexpr hwirq_t SOFTWARE_LOCAL_IRQ = 3;
        static constexpr hwirq_t CLOCK_LOCAL_IRQ    = 7;
        static constexpr hwirq_t EXTERNAL_LOCAL_IRQ = 9;

        /**
         * @brief 创建一个 CPU 本地中断驱动.
         *
         * @param node 统一设备节点非拥有指针.
         * @param identifier 设备标识.
         * @param hart_id 所属 hart.
         * @return Result<util::owner<RiscVIntC*>> 创建结果.
         */
        [[nodiscard]]
        static Result<util::owner<RiscVIntC *>> create(
            device::DeviceNode *node, intc_t identifier,
            device::cpuid_t hart_id) noexcept;

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
        Result<void> ack_irq(hwirq_t hw_irq) noexcept override;
        [[nodiscard]]
        Result<void> set_trigger(hwirq_t hw_irq,
                                 device::IrqTrigger trigger) noexcept override;

    private:
        /**
         * @brief 构造一个 CPU 本地中断设备驱动.
         *
         * @param node 统一CPU 本地中断设备节点引用指针.
         * @param identifier 设备标识.
         * @param hart_id 所属 hart.
         */
        RiscVIntC(const device::DeviceNode &node, intc_t identifier,
                  device::cpuid_t hart_id) noexcept;

        intc_t _identifier = device::INVALID_ICTRL_ID;
        device::cpuid_t _hart_id   = 0;
    };
}  // namespace driver
