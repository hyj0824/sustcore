/**
 * @file plic.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief PLIC 设备与驱动
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <device/int.h>
#include <device/resource.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace driver {
    /**
     * @brief RISC-V PLIC 上下文描述.
     */
    struct PlicContext {
        device::cpuid_t hart_id      = 0;
        intc_t parent_intc   = device::INVALID_ICTRL_ID;
        virq_t external_virq = 0;
        size_t context_index         = 0;
    };

    /**
     * @brief RISC-V PLIC 驱动
     */
    class Plic final : public device::IrqChip {
    public:
        /**
         * @brief 创建一个 PLIC 驱动.
         *
         * @param node 统一设备节点非拥有指针.
         * @param identifier 设备标识.
         * @param source_count 中断源数量.
         * @param contexts context 列表.
         * @return Result<util::owner<Plic*>> 创建结果.
         */
        [[nodiscard]]
        static Result<util::owner<Plic *>> create(
            device::DeviceNode *node, intc_t identifier, hwirq_t source_count,
            std::vector<PlicContext> contexts) noexcept;

        /**
         * @brief 销毁 PLIC 驱动
         */
        ~Plic() override;

        [[nodiscard]]
        std::string_view compatible() const noexcept override;
        [[nodiscard]]
        std::vector<PhyArea> mmio_regions() const noexcept;
        [[nodiscard]]
        intc_t identifier() const noexcept;
        [[nodiscard]]
        hwirq_t source_count() const noexcept;
        [[nodiscard]]
        const std::vector<PlicContext> &contexts() const noexcept;
        [[nodiscard]]
        Result<const PlicContext &> context_for_hart(
            device::cpuid_t hart_id) const noexcept;
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
        [[nodiscard]]
        Result<void> attach_to_parent_domain(
            device::IrqManager &irqman,
            device::IrqDomain &self_domain) noexcept override;
        [[nodiscard]]
        Result<hwirq_t> resolve_claim_for_current_hart() noexcept;

    private:
        /**
         * @brief 构造一个 PLIC 驱动
         *
         * @param node PLIC 设备节点引用指针.
         * @param identifier 设备标识.
         * @param source_count 中断源数量.
         * @param contexts context 列表.
         */
        Plic(const device::DeviceNode &node, intc_t identifier,
             hwirq_t source_count,
             std::vector<PlicContext> contexts) noexcept;
        [[nodiscard]]
        Result<void> init_runtime() noexcept;

        void handle_parent_irq(const device::IrqEvent &event) noexcept;
        [[nodiscard]]
        Result<const PlicContext &> context_for_parent_virq(
            virq_t parent_virq) const noexcept;
        [[nodiscard]]
        Result<device::VIrqResource *> find_parent_virq_resource(
            virq_t parent_virq) noexcept;
        [[nodiscard]]
        Result<void> preallocate_child_virqs() noexcept;
        [[nodiscard]]
        Result<void> validate_source(hwirq_t hw_irq) const noexcept;
        sus_u32 read32(size_t offset) const noexcept;
        void write32(size_t offset, sus_u32 value) noexcept;
        [[nodiscard]]
        static constexpr size_t priority_offset(hwirq_t hw_irq) noexcept {
            return static_cast<size_t>(hw_irq) * sizeof(sus_u32);
        }
        [[nodiscard]]
        static constexpr size_t enable_offset(size_t context_index,
                                              hwirq_t hw_irq) noexcept {
            return 0x002000 + context_index * 0x80 +
                   static_cast<size_t>(hw_irq / 32) * sizeof(sus_u32);
        }
        [[nodiscard]]
        static constexpr size_t threshold_offset(size_t context_index) noexcept {
            return 0x200000 + context_index * 0x1000;
        }
        [[nodiscard]]
        static constexpr size_t claim_complete_offset(
            size_t context_index) noexcept {
            return threshold_offset(context_index) + sizeof(sus_u32);
        }

        intc_t _identifier       = device::INVALID_ICTRL_ID;
        hwirq_t _source_count    = 0;
        std::vector<PlicContext> _contexts;
        device::IrqManager *_irqman = nullptr;
        device::IrqDomain *_self_domain = nullptr;
        std::unordered_map<device::cpuid_t, size_t> _context_indices;
        std::unordered_map<device::cpuid_t, hwirq_t> _claimed_sources;
        volatile sus_u32 *_base = nullptr;
    };
}  // namespace driver
