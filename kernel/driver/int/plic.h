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
        device::cpuid_t hart_id = 0;
        intc_t parent_intc      = device::INVALID_ICTRL_ID;
        virq_t external_virq    = 0;
        size_t context_index    = 0;
    };

    /**
     * @brief RISC-V PLIC 驱动
     */
    class Plic final : public device::IrqChip {
    public:
        /**
         * @brief 创建一个 PLIC 驱动.
         *
         * @param res 统一设备节点与资源集合.
         * @param identifier 设备标识.
         * @param source_count 中断源数量.
         * @param contexts context 列表.
         * @return Result<util::owner<Plic*>> 创建结果.
         */
        [[nodiscard]]
        static Result<util::owner<Plic *>> create(
            ResPack res, intc_t identifier, hwirq_t source_count,
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
         * @param res PLIC 设备节点与资源集合.
         * @param identifier 设备标识.
         * @param source_count 中断源数量.
         * @param contexts context 列表.
         */
        Plic(ResPack res, intc_t identifier, hwirq_t source_count,
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
        [[nodiscard]]
        Result<void> validate_context(size_t context_index) const noexcept;
        [[nodiscard]]
        Result<void> validate_context_source(size_t context_index,
                                             hwirq_t hw_irq) const noexcept;

        intc_t _identifier    = device::INVALID_ICTRL_ID;
        hwirq_t _source_count = 0;
        std::vector<PlicContext> _contexts;
        device::IrqManager *_irqman     = nullptr;
        device::IrqDomain *_self_domain = nullptr;
        std::unordered_map<device::cpuid_t, size_t> _context_indices;
        std::unordered_map<device::cpuid_t, hwirq_t> _claimed_sources;
        volatile char *_base = nullptr;
        size_t _mmio_range = 0;
        size_t _max_supported_contexts = 0;

        // PLIC 一共支持 15872 个上下文和 1023 个中断源
        constexpr static size_t PLIC_MAX_CONTEXTS = 15872;
        constexpr static size_t PLIC_MAX_SOURCES  = 1023;

        constexpr static size_t PLIC_SRCBITS_SIZE    = 128;
        constexpr static size_t PLIC_SRCBITS_ENTRIES = 32;

        static_assert(PLIC_SRCBITS_SIZE == (PLIC_MAX_SOURCES + 1) / 8,
                      "PLIC source bits size mismatch!");
        static_assert(PLIC_SRCBITS_ENTRIES == PLIC_SRCBITS_SIZE / 4,
                      "PLIC source bits entries mismatch!");

        // interrupt priority
        using intprio_t                           = dword;
        constexpr static size_t PLIC_INTPRIO_SIZE = 0x1000;
        // 中断源 x 对应的优先级寄存器是 _intprios_base->int_prios[x]
        struct IntPrios {
            intprio_t int_prios[PLIC_MAX_SOURCES + 1];
        };
        static_assert(sizeof(IntPrios) == PLIC_INTPRIO_SIZE,
                      "PLIC IntPrios struct size mismatch!");
        static_assert(sizeof(intprio_t) == 4, "intprio_t size mismatch!");

        // ipb: intterupt pending bits
        using ipb_t = dword;
        struct IPBs {
            ipb_t ipb[PLIC_SRCBITS_ENTRIES];
        };
        static_assert(sizeof(IPBs) == PLIC_SRCBITS_SIZE,
                      "PLIC IPBs struct size mismatch!");
        static_assert(sizeof(ipb_t) == 4, "ipb_t size mismatch!");

        // ieb: interrupt enable bits
        using ieb_t                           = dword;
        constexpr static size_t PLIC_IEB_SIZE = 0x1F0000;
        struct IEBs {
            ieb_t ieb[PLIC_MAX_CONTEXTS][PLIC_SRCBITS_ENTRIES];
        };
        static_assert(sizeof(IEBs) == PLIC_IEB_SIZE,
                      "PLIC IEBs struct size mismatch!");
        static_assert(PLIC_IEB_SIZE == PLIC_MAX_CONTEXTS * PLIC_SRCBITS_SIZE,
                      "PLIC IEBs size mismatch!");
        static_assert(sizeof(ieb_t) == 4, "ieb_t size mismatch!");

        // crsr : context-related setting registers
        constexpr static size_t PLIC_CRSR_SIZE  = 0x1000;
        constexpr static size_t PLIC_THRESHOLD  = 0x0;
        constexpr static size_t PLIC_CLAIM      = 0x4;
        constexpr static size_t PLIC_COMPLETE   = 0x4;
        constexpr static size_t PLIC_CRSRS_SIZE = 0x3e00000;
        struct CRSR {
            intprio_t threshold;
            union {
                intprio_t claim;
                intprio_t complete;
            };
            char padding[PLIC_CRSR_SIZE - 2 * sizeof(intprio_t)];
        };
        static_assert(sizeof(CRSR) == PLIC_CRSR_SIZE,
                      "PLIC crsr struct size mismatch!");
        static_assert(sizeof(intprio_t) == 4, "intprio_t size mismatch!");
        static_assert(offsetof(CRSR, threshold) == PLIC_THRESHOLD,
                      "threshold offset mismatch!");
        static_assert(offsetof(CRSR, claim) == PLIC_CLAIM,
                      "claim offset mismatch!");
        static_assert(offsetof(CRSR, complete) == PLIC_COMPLETE,
                      "complete offset mismatch!");
        struct CRSRs {
            CRSR crsrs[PLIC_MAX_CONTEXTS];
        };
        static_assert(sizeof(CRSRs) == PLIC_CRSRS_SIZE,
                      "PLIC CRSRs struct size mismatch!");
        static_assert(PLIC_CRSRS_SIZE == PLIC_MAX_CONTEXTS * PLIC_CRSR_SIZE,
                      "PLIC CRSRs size mismatch!");

        // int prios base = base + PLIC_INTPRIO
        constexpr static size_t PLIC_INTPRIO = 0x0;
        volatile IntPrios *_intprios_base    = nullptr;
        // ipb base = base + PLIC_IPB
        constexpr static size_t PLIC_IPB     = 0x1000;
        volatile IPBs *_ipbs_base            = nullptr;
        // ieb base = base + PLIC_IEB
        constexpr static size_t PLIC_IEB     = 0x2000;
        volatile IEBs *_iebs_base            = nullptr;
        // crsrs base = base + PLIC_CRSR
        constexpr static size_t PLIC_CRSR    = 0x200000;
        volatile CRSRs *_crsrs_base          = nullptr;
    };
}  // namespace driver
