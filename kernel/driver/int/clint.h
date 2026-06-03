/**
 * @file clint.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief CLINT 设备与驱动
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <device/int.h>
#include <string>
#include <utility>
#include <vector>

namespace driver {
    /**
     * @brief RISC-V CLINT 驱动
     */
    class Clint final : public device::IrqChip {
    public:
        /**
         * @brief 创建一个 CLINT 设备包装对象.
         *
         * @param res 统一设备节点与资源集合.
         * @param identifier 设备标识.
         * @param hart_id 默认 hart.
         * @param target_harts 目标 hart 集合.
         * @return Result<util::owner<Clint*>> 创建结果.
         */
        [[nodiscard]]
        static Result<util::owner<Clint *>> create(
            DevRes res, intc_t identifier, device::cpuid_t hart_id,
            std::vector<device::cpuid_t> target_harts) noexcept;

        /**
         * @brief 销毁设备包装对象.
         */
        ~Clint() override = default;

        [[nodiscard]]
        std::string_view compatible() const noexcept override;
        [[nodiscard]]
        intc_t identifier() const noexcept;
        [[nodiscard]]
        device::cpuid_t hart_id() const noexcept;
        [[nodiscard]]
        const std::vector<device::cpuid_t> &target_harts() const noexcept;
        [[nodiscard]]
        bool supports_hart(device::cpuid_t hart_id) const noexcept;
        [[nodiscard]]
        virq_t software_virq() const noexcept;
        [[nodiscard]]
        virq_t clock_virq() const noexcept;
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
                                 device::IrqTrigger trigger) noexcept override;

    private:
        /**
         * @brief 构造一个 CLINT 驱动
         *
         * @param res 统一 Clint 设备节点与资源集合.
         * @param identifier 设备标识.
         * @param hart_id 默认 hart.
         * @param target_harts 目标 hart 集合.
         */
        Clint(DevRes res, intc_t identifier,
              device::cpuid_t hart_id,
              std::vector<device::cpuid_t> target_harts) noexcept;

        intc_t _identifier = device::INVALID_ICTRL_ID;
        device::cpuid_t _hart_id = 0;
        std::vector<device::cpuid_t> _target_harts;
    };
}  // namespace driver
