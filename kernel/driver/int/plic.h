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

#include <device/device.h>
#include <device/model.h>
#include <device/resource.h>
#include <driver/int/base.h>

#include <unordered_map>
#include <vector>

namespace driver {
    /**
     * @brief RISC-V PLIC 驱动
     */
    class Plic final : public IrqChip {
    public:
        // PLIC 上下文编号
        using ctx_t = size_t;

        /**
         * @brief RISC-V PLIC 上下文描述.
         */
        struct Context {
            // context 的 hart_id 与 virq
            // 每个 virq 都唯一对应了一个 context
            device::cpuid_t hart_id = 0;
            virq_t external_virq    = 0;
            ctx_t ctx_id            = 0;
            // 该 context 是否被启用
            bool enabled            = false;
            // 该 context 是否处理完毕
            bool completed          = true;
        };

        /**
         * @brief 销毁 PLIC 驱动
         */
        ~Plic() override;

        [[nodiscard]]
        std::string_view compatible() const noexcept override;
        [[nodiscard]]
        intc_t identifier() const noexcept;
        [[nodiscard]]
        hwirq_t srccnt() const noexcept;
        [[nodiscard]]
        Result<void> enable_irq(hwirq_t hw_irq) noexcept override;
        [[nodiscard]]
        Result<void> disable_irq(hwirq_t hw_irq) noexcept override;
        [[nodiscard]]
        Result<void> set_priority(hwirq_t hw_irq,
                                  irq_prio_t prio) noexcept override;
        [[nodiscard]]
        Result<void> set_affinity(hwirq_t hw_irq,
                                  cpu_mask_t mask) noexcept override;
        [[nodiscard]]
        Result<void> ack(const IrqEvent &event) noexcept override;
        [[nodiscard]]
        Result<void> set_trigger(hwirq_t hw_irq,
                                 IrqTrigger trigger) noexcept override;

    private:
        // 为特定的 context 启用中断
        [[nodiscard]]
        Result<void> enable_irq_for(ctx_t ctx, hwirq_t hw_irq) noexcept;
        [[nodiscard]]
        Result<void> disable_irq_for(ctx_t ctx, hwirq_t hw_irq) noexcept;

        // 基本信息
        // PLIC 一共支持 15872 个上下文和 1023 个中断源
        constexpr static size_t PLIC_MAX_CONTEXTS           = 15872;
        constexpr static size_t PLIC_MAX_SOURCES            = 1023;
        constexpr static const char *PLIC_COMPATIBLE_STRING = "riscv,plic0";

        // 字段
        intc_t _identifier = device::INVALID_ICTRL_ID;
        size_t _srccnt     = 0;
        size_t _ctxcnt     = 0;
        ctx_t _first_enabled_ctx = PLIC_MAX_CONTEXTS;
        // 上下文
        std::vector<Context> _contexts;
        // virq -> context 映射
        std::unordered_map<virq_t, ctx_t> _virq_to_context;
        // 中断域
        IrqDomain *_domain = nullptr;

        // 根据父域 virq 获得对应的 context
        // 每一个链接到父域的 virq 都对应一个 context
        [[nodiscard]]
        Result<Context &> resolve_ctx(virq_t parent_virq) const noexcept;

        // 根据父域 virq 获得对应的 virq 资源
        [[nodiscard]]
        Result<device::VIrqResource *> resolve_virq_resource(
            virq_t parent_virq) const noexcept;

        // 验证用(虽然现在没用)
        // 某个中断被哪个 context 所 claim
        ctx_t _claimlist[PLIC_MAX_SOURCES + 1] = {};
        /**
         * @brief 去 claim 当前待处理的中断事件
         *
         * 同时只能有一个核进入 claim 状态
         * claim() 将会把目前发生的一个事件认领给传入的 context
         * 当没有事件时, claim() 返回 0
         *
         * @return hwirq_t claim 结果, 无事件时返回 0
         */
        hwirq_t claim(Context &ctx) noexcept;

        // 处理来自父域的中断事件
        // 这意味着有一个外部中断发生, PLIC 将该中断二次分发
        // 流程是: plic_virq -> plic 中断事件 ->
        // plic 读取硬件中真正的 hw_irq -> real_virq -> 真实中断事件
        void handle_parent_irq(const IrqEvent &event) noexcept;

        // 确认 source 是否有效
        [[nodiscard]]
        Result<void> validate_source(hwirq_t hw_irq) const noexcept;
        // 确认 context 是否有效
        [[nodiscard]]
        Result<void> validate_context(size_t ctx_id) const noexcept;

        static IrqManager &irqman() {
            return device::DeviceModel::inst().interrupt();
        }

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
        constexpr static size_t PLIC_IEBS_SIZE = 0x1F0000;
        constexpr static size_t PLIC_IEB_SIZE = 0x80;
        struct IEBs {
            ieb_t ieb[PLIC_MAX_CONTEXTS][PLIC_SRCBITS_ENTRIES];
        };
        static_assert(sizeof(IEBs) == PLIC_IEBS_SIZE,
                      "PLIC IEBs struct size mismatch!");
        static_assert(sizeof(std::declval<IEBs>().ieb[0]) == PLIC_IEB_SIZE,
                      "PLIC IEB struct size mismatch!");
        static_assert(PLIC_IEBS_SIZE == PLIC_MAX_CONTEXTS * PLIC_SRCBITS_SIZE,
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
                dword claim;
                dword complete;
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

        // 基址
        volatile char *_base                 = nullptr;
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

        /**
         * @brief 构造一个 PLIC 驱动
         *
         * @param res PLIC 设备节点与资源集合.
         * @param identifier 设备标识.
         * @param srccnt 中断源数量.
         * @param contexts context 列表.
         */
        Plic(DevRes res, intc_t identifier, hwirq_t srccnt,
             std::vector<Context> contexts, volatile char *base) noexcept;

        template <typename T>
        static volatile T *resolve_base(volatile char *base,
                                        size_t offset) noexcept {
            return reinterpret_cast<volatile T *>(base + offset);
        }

        // 初始化 PLIC
        /**
         * @brief 初始化 PLIC 运行时状态与中断域关联.
         *
         * @param domain PLIC 对应的中断域.
         * @return Result<void> 初始化结果.
         */
        [[nodiscard]]
        Result<void> initialize(IrqDomain *domain) noexcept;

    public:
        /**
         * @brief 创建一个 PLIC 驱动.
         *
         * @param devres 统一设备节点与资源集合.
         * @param identifier 设备标识.
         * @param srccnt 中断源数量 (ndev in fdt)
         * @return Result<util::owner<Plic*>> 创建结果.
         */
        [[nodiscard]]
        static Result<util::owner<Plic *>> create(
            DevRes devres, intc_t identifier, hwirq_t srccnt,
            std::vector<Context> ctxs) noexcept;

        [[nodiscard]]
        Result<void> attach_to_parent_domain(
            IrqManager &irqman, IrqDomain &self_domain) noexcept override;
    };
}  // namespace driver
