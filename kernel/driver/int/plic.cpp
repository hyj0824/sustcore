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

#include <device/resource.h>
#include <driver/int/plic.h>
#include <env.h>
#include <logger.h>
#include <sus/raii.h>

namespace driver {
    /**
     * @brief 销毁 PLIC 驱动.
     */
    Plic::~Plic() {
        _domain        = nullptr;
        _base          = nullptr;
        _intprios_base = nullptr;
        _ipbs_base     = nullptr;
        _iebs_base     = nullptr;
        _crsrs_base    = nullptr;
    }

    std::string_view Plic::compatible() const noexcept {
        return PLIC_COMPATIBLE_STRING;
    }

    [[nodiscard]]
    intc_t Plic::identifier() const noexcept {
        return _identifier;
    }
    [[nodiscard]]
    hwirq_t Plic::srccnt() const noexcept {
        return _srccnt;
    }
    [[nodiscard]]
    Result<void> Plic::enable_irq(hwirq_t hw_irq) noexcept {
        loggers::INTERRUPT::DEBUG("PLIC %d: 尝试启用 hw_irq %d", _identifier, hw_irq);
        // TODO: 支持根据 affinity 来为特定的 context 启用中断
        auto validate_res = validate_source(hw_irq);
        propagate(validate_res);
        auto ctx_res = validate_context(_first_enabled_ctx);
        propagate(ctx_res);
        return enable_irq_for(_first_enabled_ctx, hw_irq);
    }
    [[nodiscard]]
    Result<void> Plic::disable_irq(hwirq_t hw_irq) noexcept {
        loggers::INTERRUPT::DEBUG("PLIC %d: 尝试禁用 hw_irq %d", _identifier, hw_irq);
        // TODO: 支持根据 affinity 来为特定的 context 禁用中断
        auto validate_res = validate_source(hw_irq);
        propagate(validate_res);
        auto ctx_res = validate_context(_first_enabled_ctx);
        propagate(ctx_res);
        return disable_irq_for(_first_enabled_ctx, hw_irq);
    }
    [[nodiscard]]
    Result<void> Plic::set_priority(hwirq_t hw_irq, irq_prio_t prio) noexcept {
        if (prio >= 8) {
            loggers::INTERRUPT::ERROR("无效的优先级: %d, 有效范围: [0, 7]",
                                      prio);
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        // 为特定 hw_irq 的 int_prio 加锁
        auto validate_res = validate_source(hw_irq);
        propagate(validate_res);
        _intprios_base->int_prios[hw_irq] = prio;
        void_return();
    }
    [[nodiscard]]
    Result<void> Plic::set_affinity(hwirq_t hw_irq, cpu_mask_t mask) noexcept {
        (void)hw_irq;
        (void)mask;
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }
    [[nodiscard]]
    Result<void> Plic::ack(const IrqEvent &event) noexcept {
        // 此处 event.chip_specific[0] 是该 hw_irq 对应的 ctx_id
        auto validate_res = validate_source(event.hw_irq);
        propagate(validate_res);
        auto ctx_index = event.chip_specific[0];
        validate_res   = validate_context(event.chip_specific[0]);
        propagate(validate_res);

        // 验证该 context 是否真的 claim 了这个 hw_irq
        if (ctx_index != _claimlist[event.hw_irq]) {
            loggers::INTERRUPT::ERROR(
                "hw_irq %d 没有被 context %d claim, claim 结果: %d",
                event.hw_irq, ctx_index, _claimlist[event.hw_irq]);
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        // 设置 complete
        volatile CRSR &crsr            = _crsrs_base->crsrs[ctx_index];
        crsr.complete                  = event.hw_irq;
        _contexts[ctx_index].completed = true;
        _claimlist[event.hw_irq] =
            PLIC_MAX_CONTEXTS;  // 重置 claim 结果, 避免误用

        loggers::INTERRUPT::DEBUG(
            "PLIC %d: 已确认 hw_irq %d, context %d 已完成处理",
            _identifier, event.hw_irq, ctx_index);
        void_return();
    }
    [[nodiscard]]
    Result<void> Plic::set_trigger(hwirq_t hw_irq,
                                   IrqTrigger trigger) noexcept {
        // PLIC 只支持上升沿触发
        if (trigger != IrqTrigger::EDGE_RISING) {
            loggers::INTERRUPT::ERROR("PLIC 不支持的触发方式: %d",
                                      static_cast<int>(trigger));
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }
        void_return();
    }

    hwirq_t Plic::claim(Context &ctx) noexcept {
        auto ctx_id = ctx.ctx_id;
        if (ctx_id >= _ctxcnt) {
            loggers::INTERRUPT::ERROR(
                "非法的 context 索引: %d, context 数量: %d", ctx_id, _ctxcnt);
            return 0;
        }
        if (!ctx.enabled) {
            loggers::INTERRUPT::ERROR("context %d 未启用, 无法 claim 中断事件",
                                      ctx_id);
            return 0;
        }
        // 上一个事件尚未处理完毕, 无法 claim 新事件
        if (!ctx.completed) {
            return 0;
        }
        // TODO: 给 claim 加锁
        volatile CRSR &crsr = _crsrs_base->crsrs[ctx_id];
        auto claimed_irq    = crsr.claim;
        if (claimed_irq != 0) {
            // 记录 claim 结果, 用于验证
            _claimlist[claimed_irq] = ctx_id;
            ctx.completed           = false;
        }
        return claimed_irq;
    }

    /**
     * @brief 根据父域 virq 解析对应的 context.
     */
    Result<Plic::Context &> Plic::resolve_ctx(
        virq_t parent_virq) const noexcept {
        auto ctx_it = _virq_to_context.find(parent_virq);
        if (ctx_it == _virq_to_context.end()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        if (ctx_it->second >= _contexts.size()) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        return std::ref(const_cast<Context &>(_contexts[ctx_it->second]));
    }

    /**
     * @brief 根据父域 virq 解析对应的 virq 资源.
     */
    Result<device::VIrqResource *> Plic::resolve_virq_resource(
        virq_t parent_virq) const noexcept {
        for (const auto &resource : virq_resources()) {
            if (resource != nullptr && resource->virq() == parent_virq) {
                return resource.get();
            }
        }
        unexpect_return(ErrCode::ENTRY_NOT_FOUND);
    }

    /**
     * @brief 校验中断源编号是否合法.
     */
    Result<void> Plic::validate_source(hwirq_t hw_irq) const noexcept {
        if (hw_irq == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (hw_irq > _srccnt || hw_irq > PLIC_MAX_SOURCES) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        void_return();
    }

    /**
     * @brief 校验 context 编号是否合法.
     */
    Result<void> Plic::validate_context(size_t ctx_id) const noexcept {
        if (ctx_id >= _ctxcnt || ctx_id >= PLIC_MAX_CONTEXTS) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        if (mmio_resources().empty() || mmio_resources().front() == nullptr) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        size_t mmio_range = mmio_resources().front()->region().size();
        if (mmio_range <= PLIC_CRSR) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        size_t max_contexts = (mmio_range - PLIC_CRSR) / sizeof(CRSR);
        if (ctx_id >= max_contexts) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        void_return();
    }

    // 为特定的 context 启用中断
    [[nodiscard]]
    Result<void> Plic::enable_irq_for(ctx_t ctx, hwirq_t hw_irq) noexcept {
        auto validate_res = validate_context(ctx);
        propagate(validate_res);
        // TODO: 为特定 hw_irq 的 ieb 加锁
        size_t hwirq_dword                 = hw_irq / 32;
        size_t hwirq_bit                   = hw_irq % 32;
        ieb_t mask                         = 1u << hwirq_bit;
        _iebs_base->ieb[ctx][hwirq_dword] |= mask;
        void_return();
    }
    [[nodiscard]]
    Result<void> Plic::disable_irq_for(ctx_t ctx, hwirq_t hw_irq) noexcept {
        auto validate_res = validate_context(ctx);
        propagate(validate_res);
        // TODO: 为特定 hw_irq 的 ieb 加锁
        size_t hwirq_dword                 = hw_irq / 32;
        size_t hwirq_bit                   = hw_irq % 32;
        ieb_t mask                         = 1u << hwirq_bit;
        _iebs_base->ieb[ctx][hwirq_dword] &= ~mask;
        void_return();
    }

    void Plic::handle_parent_irq(const IrqEvent &event) noexcept {
        // auto acker
        util::Guard acker([event]() {
            auto res = irqman().ack(event);
            if (!res.has_value()) {
                loggers::INTERRUPT::ERROR("无法应答父域 virq %d, 错误码: %s",
                                          event.virq, to_cstring(res.error()));
            }
        });

        // 获得对应 context
        auto ctx_res = resolve_ctx(event.virq);
        if (!ctx_res.has_value()) {
            loggers::INTERRUPT::ERROR(
                "无法解析父域 virq %d 对应的 context, 错误码: %s", event.virq,
                to_cstring(ctx_res.error()));
            return;
        }

        auto ctx = ctx_res.value().get();
        assert(0 <= ctx.ctx_id && ctx.ctx_id < _ctxcnt);

        // claim 一个 hw_irq
        auto claimed_irq = claim(ctx);
        // 无可处理的事件
        if (claimed_irq == 0) {
            return;
        }

        auto virq = _domain->to_virq(claimed_irq);
        if (!virq.has_value()) {
            loggers::INTERRUPT::ERROR(
                "无法将 claimed hw_irq %d 转换为 virq, 错误码: %s", claimed_irq,
                to_cstring(virq.error()));
            return;
        }

        loggers::INTERRUPT::DEBUG(
            "PLIC[%d]二级分发: context=%d claimed hw_irq=%d -> virq=%d",
            _identifier, ctx.ctx_id, claimed_irq, virq.value());

        IrqEvent e = {
            .virq          = virq.value(),
            .hw_irq        = claimed_irq,
            .domain        = _domain,
            .chip_specific = {ctx.ctx_id, 0},
        };
        auto dispatch_res = irqman().dispatch(e);
        if (!dispatch_res.has_value()) {
            loggers::INTERRUPT::ERROR(
                "分发 PLIC 中断事件失败: virq=%d hw_irq=%d, 错误码: %s", e.virq,
                e.hw_irq, to_cstring(dispatch_res.error()));
        }
    }

    Plic::Plic(DevRes res, intc_t identifier, hwirq_t srccnt,
               std::vector<Context> contexts, volatile char *base) noexcept
        : IrqChip(std::move(res)),
          _identifier(identifier),
          _srccnt(srccnt),
          _ctxcnt(contexts.size()),
          _contexts(std::move(contexts)),
          _virq_to_context(),
          _intprios_base(resolve_base<IntPrios>(base, PLIC_INTPRIO)),
          _ipbs_base(resolve_base<IPBs>(base, PLIC_IPB)),
          _iebs_base(resolve_base<IEBs>(base, PLIC_IEB)),
          _crsrs_base(resolve_base<CRSRs>(base, PLIC_CRSR)) {
        assert (base != nullptr);
        for (auto &_claim : _claimlist) {
            _claim = PLIC_MAX_CONTEXTS;
        }
    }

    Result<void> Plic::initialize(IrqDomain *domain) noexcept {
        if (domain == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        this->_domain      = domain;
        _first_enabled_ctx = PLIC_MAX_CONTEXTS;
        // 第一个出错的错误码, 用于返回
        ErrCode err        = ErrCode::SUCCESS;
        auto update_err    = [&err](ErrCode new_err) {
            if (err == ErrCode::SUCCESS) {
                err = new_err;
            }
        };

        for (const auto &ctx : _contexts) {
            if (!ctx.enabled) {
                continue;
            }
            _first_enabled_ctx = ctx.ctx_id;
            loggers::INTERRUPT::DEBUG(
                "Plic[%u] 选择首个 enabled context=%u", identifier(),
                static_cast<unsigned>(_first_enabled_ctx));
            break;
        }
        if (_first_enabled_ctx == PLIC_MAX_CONTEXTS) {
            loggers::INTERRUPT::ERROR(
                "Plic[%u] 初始化失败: 没有可用于 enable/disable 的 enabled "
                "context",
                identifier());
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        // 禁用全部的 irq
        for (size_t ctx_id = 0; ctx_id < _ctxcnt; ++ctx_id) {
            for (size_t hw_irq = 1; hw_irq <= _srccnt; ++hw_irq) {
                auto dis_res = disable_irq_for(ctx_id, hw_irq);
                if (!dis_res.has_value()) {
                    loggers::INTERRUPT::ERROR(
                        "初始化 PLIC 失败: 无法禁用 context %d 的 hw_irq %d, "
                        "错误码: %s",
                        ctx_id, hw_irq, to_cstring(dis_res.error()));
                    update_err(dis_res.error());
                }
            }
        }

        // 将全部 irq 的优先级设置为 1
        for (size_t i = 1; i <= _srccnt; ++i) {
            auto set_res = set_priority(i, 1);
            if (!set_res.has_value()) {
                loggers::INTERRUPT::ERROR(
                    "初始化 PLIC 失败: 无法设置 hw_irq %d 的优先级, 错误码: %s",
                    i, to_cstring(set_res.error()));
                update_err(set_res.error());
            }
        }

        // 设置全部 context 的 threshold 为 0
        for (size_t ctx_id = 0; ctx_id < _ctxcnt; ++ctx_id) {
            volatile CRSR &crsr = _crsrs_base->crsrs[ctx_id];
            crsr.threshold      = 0;
        }

        // 注册全部的 virq 到 domain
        for (size_t i = 1; i <= _srccnt; ++i) {
            auto virq_res = domain->to_virq(i);
            if (!virq_res.has_value()) {
                auto alloc_res = irqman().allocate_virq(
                    domain->id(), static_cast<hwirq_t>(i));
                if (!alloc_res.has_value()) {
                    loggers::INTERRUPT::ERROR(
                        "初始化 PLIC 失败: 无法注册 hw_irq %d 到 IrqDomain, "
                        "错误码: %s",
                        i, to_cstring(alloc_res.error()));
                    update_err(alloc_res.error());
                }
            }
        }

        if (err != ErrCode::SUCCESS) {
            unexpect_return(err);
        }
        void_return();
    }

    /**
     * @brief 创建一个 PLIC 驱动.
     *
     * @param devres 统一设备节点与资源集合.
     * @param identifier 设备标识.
     * @param source_count 中断源数量 (ndev in fdt)
     * @return Result<util::owner<Plic*>> 创建结果.
     */
    [[nodiscard]]
    Result<util::owner<Plic *>> Plic::create(
        DevRes devres, intc_t identifier, hwirq_t srccnt,
        std::vector<Context> ctxs) noexcept {
        // 从 devres 中解析资源
        // 首先确保 mmio 资源与 virq 资源均有效
        if (devres.mmios.empty()) {
            loggers::INTERRUPT::ERROR("创建 PLIC 失败: 设备资源缺少 mmio");
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (devres.virqs.empty()) {
            loggers::INTERRUPT::ERROR("创建 PLIC 失败: 设备资源缺少 virq");
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        if (ctxs.empty()) {
            loggers::INTERRUPT::ERROR(
                "创建 PLIC 失败: 设备资源缺少 context 列表");
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        // 获得 mmio 大小, 计算其至多能支持多少个 context
        // 其至多支持的 context 数, 实际上就是 mmio 中至多能容纳的 CRSR 数
        auto mmio = devres.mmios[0];
        size_t max_contexts =
            (mmio->region().size() - PLIC_CRSR) / PLIC_CRSR_SIZE;
        // ctxs 的数量应当小于 max_contexts
        if (ctxs.size() > max_contexts) {
            loggers::INTERRUPT::ERROR(
                "创建 PLIC 失败: 设备资源中的 virq 数量 %d 超过 mmio "
                "支持的最大 context 数 %d",
                ctxs.size(), max_contexts);
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        // 映射第一个 mmio 区域
        auto map_res = device::MMIOManager::inst().map_to_kernel(*mmio);
        propagate(map_res);
        auto base        = map_res.value().as<volatile char>();
        std::string name = devres.node->name();
        // 构造 PLIC 驱动实例
        auto plic = util::owner(new Plic(std::move(devres), identifier, srccnt,
                                         std::move(ctxs), base));
        util::Guard plic_deleter([plic]() { delete plic; });
        // 构造 IrqDomain
        auto domain = util::owner(
            new LinearIrqDomain<PLIC_MAX_SOURCES>(identifier, name, *plic));
        util::Guard domain_deleter([domain]() { delete domain; });

        // 注册 IrqDomain
        auto register_res =
            irqman().register_domain(util::owner<device::IrqDomain *>(domain));
        propagate(register_res);

        // 将 PLIC 驱动与 IrqDomain 关联并初始化 PLIC
        auto init_res = plic->initialize(domain);
        propagate(init_res);

        domain_deleter.release();
        plic_deleter.release();
        return plic;
    }

    [[nodiscard]]
    Result<void> Plic::attach_to_parent_domain(
        IrqManager &, IrqDomain &) noexcept {
        // 第一个出错的错误码, 用于返回
        ErrCode err     = ErrCode::SUCCESS;
        auto update_err = [&err](ErrCode new_err) {
            if (err == ErrCode::SUCCESS) {
                err = new_err;
            }
        };
        // 将自己与每个 context 对应的父域 virq 关联
        for (const auto &ctx : _contexts) {
            if (!ctx.enabled) {
                continue;
            }

            auto virq = ctx.external_virq;
            if (virq == 0) {
                update_err(ErrCode::INVALID_PARAM);
                continue;
            }
            auto virq_res = resolve_virq_resource(virq);
            if (!virq_res.has_value()) {
                loggers::INTERRUPT::ERROR(
                    "初始化 PLIC 失败: 无法解析 virq 资源, virq: %d ; 跳过",
                    virq);
                update_err(virq_res.error());
                continue;
            }

            // 注册 handler
            auto handler_res = virq_res.value()->register_handler(
                this_call(this, &Plic::handle_parent_irq));
            if (!handler_res.has_value()) {
                loggers::INTERRUPT::ERROR(
                    "初始化 PLIC 失败: 无法注册父域 virq %d 的 handler, "
                    "错误码: %s ; 跳过",
                    virq, to_cstring(handler_res.error()));
                update_err(handler_res.error());
                continue;
            }
            _virq_to_context[virq] = ctx.ctx_id;

            // enable virq
            auto enable_res = virq_res.value()->enable();
            if (!enable_res.has_value()) {
                loggers::INTERRUPT::ERROR(
                    "初始化 PLIC 失败: 无法启用父域 virq %d, 错误码: %s ; "
                    "跳过",
                    virq, to_cstring(enable_res.error()));
                update_err(enable_res.error());
                continue;
            }

            loggers::INTERRUPT::INFO(
                "PLIC 成功挂接到父域 virq %d, 对应 context %d", virq,
                ctx.ctx_id);
        }
        if (err != ErrCode::SUCCESS) {
            unexpect_return(err);
        }
        void_return();
    }
}  // namespace driver
