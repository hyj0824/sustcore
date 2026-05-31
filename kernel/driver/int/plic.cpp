/**
 * @file plic.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief PLIC 设备与驱动
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/model.h>
#include <driver/int/plic.h>
#include <env.h>
#include <logger.h>

namespace driver {
    namespace {
        constexpr hwirq_t PLIC_MAX_PREALLOCATABLE_HWIRQ = 255;
    }  // namespace

    /**
     * @brief 创建一个 PLIC 设备驱动.
     */
    Result<util::owner<Plic *>> Plic::create(
        device::DeviceNode *node, intc_t identifier, hwirq_t source_count,
        std::vector<PlicContext> contexts) noexcept {
        if (node == nullptr) {
            loggers::INTERRUPT::ERROR("Plic 创建失败: node 为空");
            unexpect_return(ErrCode::NULLPTR);
        }
        auto *device =
            new Plic(*node, identifier, source_count, std::move(contexts));
        if (device == nullptr) {
            loggers::INTERRUPT::ERROR("Plic 创建失败: 内存不足");
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        if (device->mmio_resources().empty()) {
            loggers::INTERRUPT::ERROR("Plic[%u] 创建失败: 缺少 MMIO 资源",
                                      identifier);
            delete device;
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        auto init_res = device->init_runtime();
        if (!init_res.has_value()) {
            delete device;
            propagate_return(init_res);
        }
        loggers::INTERRUPT::DEBUG(
            "创建 Plic 设备: id=%u source_count=%u contexts=%u name=%s",
            identifier, static_cast<unsigned>(source_count),
            static_cast<unsigned>(device->_contexts.size()), device->name());
        return util::owner<Plic *>(device);
    }

    /**
     * @brief 构造一个 PLIC 设备驱动.
     */
    Plic::Plic(const device::DeviceNode &node, intc_t identifier,
               hwirq_t source_count, std::vector<PlicContext> contexts) noexcept
        : device::IrqChip(node),
          _identifier(identifier),
          _source_count(source_count),
          _contexts(std::move(contexts)) {}

    std::string_view Plic::compatible() const noexcept {
        return "riscv,plic0";
    }

    /**
     * @brief 获取 MMIO 区域列表.
     */
    std::vector<PhyArea> Plic::mmio_regions() const noexcept {
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
    intc_t Plic::identifier() const noexcept {
        return _identifier;
    }

    /**
     * @brief 获取中断源数量.
     */
    hwirq_t Plic::source_count() const noexcept {
        return _source_count;
    }

    /**
     * @brief 获取 context 列表.
     */
    const std::vector<PlicContext> &Plic::contexts() const noexcept {
        return _contexts;
    }

    /**
     * @brief 获取指定 hart 对应的 context.
     */
    Result<const PlicContext &> Plic::context_for_hart(
        device::cpuid_t hart_id) const noexcept {
        for (const auto &context : _contexts) {
            if (context.hart_id == hart_id) {
                return std::cref(context);
            }
        }
        unexpect_return(ErrCode::ENTRY_NOT_FOUND);
    }

    /**
     * @brief 完成 PLIC 运行时接入所需的 MMIO 初始化.
     */
    Result<void> Plic::init_runtime() noexcept {
        if (!device::MMIOManager::initialized()) {
            loggers::INTERRUPT::ERROR("Plic 创建失败: MMIOManager 未初始化");
            unexpect_return(ErrCode::FAILURE);
        }
        assert(mmio_resources().front() != nullptr);
        auto base_res = device::MMIOManager::inst().map_to_kernel(
            *mmio_resources().front());
        if (!base_res.has_value()) {
            loggers::INTERRUPT::ERROR("Plic 创建失败: MMIO 映射失败 err=%s",
                                      to_cstring(base_res.error()));
            propagate_return(base_res);
        }
        _base = reinterpret_cast<volatile sus_u32 *>(base_res.value().addr());
        for (const auto &context : contexts()) {
            _context_indices[context.hart_id] = context.context_index;
            write32(Plic::threshold_offset(context.context_index), 0);
        }
        void_return();
    }

    /**
     * @brief 销毁设备驱动并释放其持有的资源.
     */
    Plic::~Plic() {
        _base        = nullptr;
        _irqman      = nullptr;
        _self_domain = nullptr;
    }

    /**
     * @brief 获取驱动名称.
     */
    Result<void> Plic::attach_to_parent_domain(
        device::IrqManager &irqman, device::IrqDomain &self_domain) noexcept {
        _irqman      = &irqman;
        _self_domain = &self_domain;

        for (const auto &context : contexts()) {
            auto virq_resource_res =
                find_parent_virq_resource(context.external_virq);
            if (!virq_resource_res.has_value()) {
                loggers::INTERRUPT::ERROR(
                    "Plic[%u] 挂接父域失败: parent_virq=%llu err=%s",
                    identifier(),
                    static_cast<unsigned long long>(context.external_virq),
                    to_cstring(virq_resource_res.error()));
                propagate_return(virq_resource_res);
            }

            // 注册分发处理器
            auto handler_res = virq_resource_res.value()->register_handler(
                this_call(this, &Plic::handle_parent_irq));
            if (!handler_res.has_value() &&
                handler_res.error() != ErrCode::KEY_DUPLICATED)
            {
                loggers::INTERRUPT::ERROR(
                    "Plic[%u] 挂接父域失败: parent_virq=%llu err=%s",
                    identifier(),
                    static_cast<unsigned long long>(context.external_virq),
                    to_cstring(handler_res.error()));
                propagate_return(handler_res);
            }
            if (!handler_res.has_value()) {
                loggers::INTERRUPT::DEBUG(
                    "Plic[%u] 父域 virq=%llu 已存在 handler, 跳过资源接管",
                    identifier(),
                    static_cast<unsigned long long>(context.external_virq));
            }
            loggers::INTERRUPT::INFO(
                "Plic[%u] 已挂接父域 virq=%llu hart=%u context=%u",
                identifier(),
                static_cast<unsigned long long>(context.external_virq),
                context.hart_id, static_cast<unsigned>(context.context_index));
        }

        // 注册全部 virq
        auto preallocate_res = preallocate_child_virqs();
        if (!preallocate_res.has_value()) {
            loggers::INTERRUPT::ERROR("Plic[%u] 预分配子 virq 失败: err=%s",
                                      identifier(),
                                      to_cstring(preallocate_res.error()));
            propagate_return(preallocate_res);
        }
        void_return();
    }

    /**
     * @brief 处理父中断域转发过来的 external virq.
     */
    void Plic::handle_parent_irq(const device::IrqEvent &event) noexcept {
        if (_irqman == nullptr || _self_domain == nullptr) {
            loggers::INTERRUPT::ERROR("Plic[%u] 父域 handler 未完成初始化",
                                      identifier());
            return;
        }

        // 根据 parent virq 查找对应 context
        auto context_res = context_for_parent_virq(event.virq);
        if (!context_res.has_value()) {
            loggers::INTERRUPT::ERROR(
                "Plic[%u] 收到未知父域 virq=%llu err=%s", identifier(),
                static_cast<unsigned long long>(event.virq),
                to_cstring(context_res.error()));
            return;
        }

        if (env::hart_ctx != nullptr &&
            context_res.value().get().hart_id !=
                static_cast<device::cpuid_t>(env::hart_ctx->hart_id()))
        {
            loggers::INTERRUPT::ERROR(
                "Plic[%u] parent virq hart 不匹配: virq=%llu expect_hart=%u "
                "current_hart=%u",
                identifier(), static_cast<unsigned long long>(event.virq),
                context_res.value().get().hart_id,
                static_cast<unsigned>(env::hart_ctx->hart_id()));
            return;
        }

        loggers::INTERRUPT::INFO(
            "Plic[%u] 处理父域中断: parent_virq=%llu hart=%u", identifier(),
            static_cast<unsigned long long>(event.virq),
            context_res.value().get().hart_id);

        // 获取中断源
        auto claim_res = resolve_claim_for_current_hart();
        if (!claim_res.has_value()) {
            loggers::INTERRUPT::ERROR("Plic[%u] claim 失败: err=%s",
                                      identifier(),
                                      to_cstring(claim_res.error()));
            return;
        }
        loggers::INTERRUPT::DEBUG("Plic[%u] claim hwirq=%u", identifier(),
                                  static_cast<unsigned>(claim_res.value()));

        // hw_irq -> virq
        auto virq_res = _self_domain->to_virq(claim_res.value());
        if (!virq_res.has_value()) {
            loggers::INTERRUPT::ERROR(
                "Plic[%u] 查找子 virq 失败: hwirq=%u err=%s", identifier(),
                static_cast<unsigned>(claim_res.value()),
                to_cstring(virq_res.error()));
            return;
        }
        loggers::INTERRUPT::DEBUG("Plic[%u] child virq=%llu hwirq=%u",
                                  identifier(),
                                  static_cast<unsigned long long>(virq_res.value()),
                                  static_cast<unsigned>(claim_res.value()));

        auto dispatch_res = _irqman->dispatch(device::IrqEvent{
            .virq    = virq_res.value(),
            .hw_irq  = claim_res.value(),
            .scause  = event.scause,
            .sepc    = event.sepc,
            .stval   = event.stval,
            .context = event.context,
        });
        if (!dispatch_res.has_value()) {
            loggers::INTERRUPT::ERROR(
                "Plic[%u] 二级分发失败: virq=%llu hwirq=%u err=%s",
                identifier(), static_cast<unsigned long long>(virq_res.value()),
                static_cast<unsigned>(claim_res.value()),
                to_cstring(dispatch_res.error()));
            return;
        }
        loggers::INTERRUPT::DEBUG(
            "Plic[%u] 完成二级分发: parent_virq=%llu child_virq=%llu hwirq=%u",
            identifier(), static_cast<unsigned long long>(event.virq),
            static_cast<unsigned long long>(virq_res.value()),
            static_cast<unsigned>(claim_res.value()));
    }

    /**
     * @brief 根据 parent virq 查找对应 context.
     */
    Result<const PlicContext &> Plic::context_for_parent_virq(
        virq_t parent_virq) const noexcept {
        for (const auto &context : contexts()) {
            if (context.external_virq == parent_virq) {
                return std::cref(context);
            }
        }
        unexpect_return(ErrCode::ENTRY_NOT_FOUND);
    }

    /**
     * @brief 从 DriverBase 持有的资源中查找父域 virq 资源.
     */
    Result<device::VIrqResource *> Plic::find_parent_virq_resource(
        virq_t parent_virq) noexcept {
        for (auto &resource : _virqs) {
            if (resource != nullptr && resource->virq() == parent_virq) {
                return resource.get();
            }
        }
        unexpect_return(ErrCode::ENTRY_NOT_FOUND);
    }

    /**
     * @brief 为当前 PLIC 域内全部有效硬件中断号预分配稳定 virq.
     */
    Result<void> Plic::preallocate_child_virqs() noexcept {
        if (_irqman == nullptr || _self_domain == nullptr) {
            unexpect_return(ErrCode::FAILURE);
        }

        if (source_count() > PLIC_MAX_PREALLOCATABLE_HWIRQ) {
            loggers::INTERRUPT::ERROR(
                "Plic[%u] source_count=%u 超出当前域可支持范围=%u",
                identifier(), static_cast<unsigned>(source_count()),
                static_cast<unsigned>(PLIC_MAX_PREALLOCATABLE_HWIRQ));
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        for (hwirq_t hw_irq = 1; hw_irq <= source_count(); ++hw_irq) {
            auto virq_res = _irqman->allocate_virq(_self_domain->id(), hw_irq);
            if (!virq_res.has_value()) {
                loggers::INTERRUPT::ERROR(
                    "Plic[%u] 预分配子 virq 失败: hwirq=%u err=%s",
                    identifier(), static_cast<unsigned>(hw_irq),
                    to_cstring(virq_res.error()));
                propagate_return(virq_res);
            }
            loggers::INTERRUPT::DEBUG(
                "Plic[%u] 预分配子 virq: hwirq=%u virq=%llu", identifier(),
                static_cast<unsigned>(hw_irq),
                static_cast<unsigned long long>(virq_res.value()));
        }

        loggers::INTERRUPT::INFO("Plic[%u] 已预分配子 virq: count=%u",
                                 identifier(),
                                 static_cast<unsigned>(source_count()));
        void_return();
    }

    /**
     * @brief 校验中断源编号是否合法.
     */
    Result<void> Plic::validate_source(hwirq_t hw_irq) const noexcept {
        if (hw_irq == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (hw_irq > source_count()) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        void_return();
    }

    /**
     * @brief 读取 32 位寄存器.
     */
    sus_u32 Plic::read32(size_t offset) const noexcept {
        assert(_base != nullptr);
        return *reinterpret_cast<volatile sus_u32 *>(
            reinterpret_cast<addr_t>(const_cast<sus_u32 *>(_base)) + offset);
    }

    /**
     * @brief 写入 32 位寄存器.
     */
    void Plic::write32(size_t offset, sus_u32 value) noexcept {
        assert(_base != nullptr);
        *reinterpret_cast<volatile sus_u32 *>(
            reinterpret_cast<addr_t>(const_cast<sus_u32 *>(_base)) + offset) =
            value;
    }

    /**
     * @brief 使能指定外部中断源.
     */
    Result<void> Plic::enable_irq(hwirq_t hw_irq) noexcept {
        auto valid_res = validate_source(hw_irq);
        propagate(valid_res);

        for (const auto &context : contexts()) {
            size_t offset = enable_offset(context.context_index, hw_irq);
            sus_u32 mask  = static_cast<sus_u32>(1u << (hw_irq % 32));
            sus_u32 value = read32(offset) | mask;
            write32(offset, value);
        }
        void_return();
    }

    /**
     * @brief 屏蔽指定外部中断源.
     */
    Result<void> Plic::disable_irq(hwirq_t hw_irq) noexcept {
        auto valid_res = validate_source(hw_irq);
        propagate(valid_res);

        for (const auto &context : contexts()) {
            size_t offset = enable_offset(context.context_index, hw_irq);
            sus_u32 mask  = static_cast<sus_u32>(1u << (hw_irq % 32));
            sus_u32 value = read32(offset) & ~mask;
            write32(offset, value);
        }
        void_return();
    }

    /**
     * @brief 设置指定外部中断源优先级.
     */
    Result<void> Plic::set_priority(hwirq_t hw_irq, domain_t prio) noexcept {
        auto valid_res = validate_source(hw_irq);
        propagate(valid_res);
        write32(priority_offset(hw_irq), prio);
        void_return();
    }

    /**
     * @brief 设置指定外部中断源亲和性.
     */
    Result<void> Plic::set_affinity(hwirq_t hw_irq, cpu_mask_t mask) noexcept {
        (void)hw_irq;
        (void)mask;
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 完成当前 hart 上已 claim 的中断源.
     */
    Result<void> Plic::ack_irq(hwirq_t hw_irq) noexcept {
        auto valid_res = validate_source(hw_irq);
        propagate(valid_res);

        if (env::hart_ctx == nullptr) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        auto hart_id = static_cast<device::cpuid_t>(env::hart_ctx->hart_id());
        auto context_res = context_for_hart(hart_id);
        propagate(context_res);

        auto claimed_it = _claimed_sources.find(hart_id);
        if (claimed_it == _claimed_sources.end()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        if (claimed_it->second != hw_irq) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        write32(claim_complete_offset(context_res.value().get().context_index),
                hw_irq);
        _claimed_sources.erase(claimed_it);
        void_return();
    }

    /**
     * @brief 设置指定外部中断源触发方式.
     */
    Result<void> Plic::set_trigger(hwirq_t hw_irq,
                                   device::IrqTrigger trigger) noexcept {
        (void)hw_irq;
        (void)trigger;
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 从当前 hart 对应 context claim 一个中断源.
     */
    Result<hwirq_t> Plic::resolve_claim_for_current_hart() noexcept {
        if (env::hart_ctx == nullptr) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        // 获得核心和上下文
        auto hart_id = static_cast<device::cpuid_t>(env::hart_ctx->hart_id());
        auto context_res = context_for_hart(hart_id);
        propagate(context_res);

        // 读取 claim 寄存器获得中断源编号
        auto source = static_cast<hwirq_t>(read32(
            claim_complete_offset(context_res.value().get().context_index)));
        if (source == 0) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        // 校验中断源编号是否合法
        auto valid_res = validate_source(source);
        propagate(valid_res);
        _claimed_sources[hart_id] = source;
        return source;
    }
}  // namespace driver
