/**
 * @file int.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief intterrupt
 * @version alpha-1.0.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <arch/description.h>
#include <device/clock.h>
#include <logger.h>
#include <sus/units.h>
#include <sustcore/errcode.h>

#include <string>
#include <unordered_map>
#include <vector>

class InterruptGuard {
private:
    bool entered      = false;
    bool prev_enabled = false;

public:
    InterruptGuard() = default;

    /**
     * @brief 进入中断关闭保护区.
     */
    void enter() {
        if (entered) {
            return;
        }
        prev_enabled = Interrupt::enabled();
        Interrupt::cli();
        entered = true;
    }

    /**
     * @brief 退出保护区并恢复中断状态.
     */
    ~InterruptGuard() {
        if (entered && prev_enabled) {
            Interrupt::sti();
        }
    }
};
namespace device {

    /**
     * @brief 中断触发方式.
     */
    enum class IrqTrigger { EDGE_RISING, EDGE_FALLING, LEVEL_HIGH, LEVEL_LOW };

    using irq_prio_t = b32;
    using cpu_mask_t = b64;
    using virq_t     = b64;
    using hwirq_t    = b32;
    using ictrl_t    = b32;
    using cpuid_t                             = b32;
    inline constexpr ictrl_t INVALID_ICTRL_ID = static_cast<ictrl_t>(-1);

    /**
     * @brief 中断控制器抽象接口.
     */
    class IntCtrl {
    public:
        virtual ~IntCtrl() = default;
        [[nodiscard]]
        virtual const char *name() const = 0;
        [[nodiscard]]
        virtual std::vector<PhyArea> mmio_regions() const = 0;
        [[nodiscard]]
        virtual Result<void> enable_irq(hwirq_t hw_irq) = 0;
        [[nodiscard]]
        virtual Result<void> disable_irq(hwirq_t hw_irq) = 0;
        [[nodiscard]]
        virtual Result<void> set_priority(hwirq_t hw_irq, irq_prio_t prio) = 0;
        [[nodiscard]]
        virtual Result<void> set_affinity(hwirq_t hw_irq, cpu_mask_t mask) = 0;
    };

    /**
     * @brief 中断控制器注册与查询
     */
    class IntCtrlManager {
    private:
        std::unordered_map<ictrl_t, util::owner<IntCtrl *>> _controllers;

        [[nodiscard]]
        constexpr hwirq_t extract_hwirq(virq_t virq) const {
            return static_cast<hwirq_t>(virq & 0xFFFFFFFF);
        }

        [[nodiscard]]
        constexpr ictrl_t extract_ictrl(virq_t virq) const {
            return static_cast<ictrl_t>((virq >> 32) & 0xFFFFFFFF);
        }

        [[nodiscard]]
        constexpr virq_t assemble_virq(ictrl_t ictrl, hwirq_t hw_irq) const {
            return static_cast<virq_t>(hw_irq |
                                       (static_cast<virq_t>(ictrl) << 32));
        }

    public:
        IntCtrlManager() = default;

        /**
         * @brief 注册中断控制器.
         */
        Result<void> register_controller(util::owner<IntCtrl *> controller,
                                         ictrl_t identifier) {
            if (_controllers.find(identifier) != _controllers.end()) {
                loggers::INTERRUPT::ERROR("中断控制器 ID: %u 已存在!",
                                          controller->name(), identifier);
                unexpect_return(ErrCode::KEY_DUPLICATED);
            }
            loggers::INTERRUPT::INFO("注册中断控制器: %s (ID: %u)",
                                     controller->name(), identifier);

            _controllers[identifier] = std::move(controller);
            void_return();
        }

        /**
         * @brief 按控制器 ID 获取控制器.
         */
        [[nodiscard]]
        Result<IntCtrl &> get_controller(ictrl_t identifier) const {
            auto it = _controllers.find(identifier);
            if (it == _controllers.end()) {
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }
            return std::ref(*it->second);
        }

        /**
         * @brief 从虚拟中断号中解析控制器 ID.
         */
        [[nodiscard]]
        Result<ictrl_t> find_controller(virq_t virq) const {
            ictrl_t ictrl_id = extract_ictrl(virq);
            if (_controllers.find(ictrl_id) == _controllers.end()) {
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }
            return ictrl_id;
        }

        /**
         * @brief 从虚拟中断号获取控制器.
         */
        [[nodiscard]]
        Result<IntCtrl &> get_controller_from_virq(virq_t virq) const {
            return find_controller(virq).and_then(
                [this](ictrl_t ictrl_id) { return get_controller(ictrl_id); });
        }

        /**
         * @brief 组合虚拟中断号.
         */
        [[nodiscard]]
        Result<virq_t> make_virq(ictrl_t ictrl, hwirq_t hw_irq) {
            if (_controllers.find(ictrl) == _controllers.end()) {
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }
            return assemble_virq(ictrl, hw_irq);
        }

        /**
         * @brief 使能虚拟中断.
         */
        [[nodiscard]]
        Result<void> enable_irq(virq_t virq) {
            auto ictrl_id = extract_ictrl(virq);
            auto hw_irq   = extract_hwirq(virq);
            auto ctrl_res = get_controller(ictrl_id);
            if (!ctrl_res.has_value()) {
                propagate_return(ctrl_res);
            }
            return ctrl_res.value().get().enable_irq(hw_irq);
        }

        /**
         * @brief 屏蔽虚拟中断.
         */
        [[nodiscard]]
        Result<void> disable_irq(virq_t virq) {
            auto ictrl_id = extract_ictrl(virq);
            auto hw_irq   = extract_hwirq(virq);
            auto ctrl_res = get_controller(ictrl_id);
            if (!ctrl_res.has_value()) {
                propagate_return(ctrl_res);
            }
            return ctrl_res.value().get().disable_irq(hw_irq);
        }

        /**
         * @brief 设置虚拟中断优先级.
         */
        [[nodiscard]]
        Result<void> set_priority(virq_t virq, irq_prio_t prio) {
            auto ictrl_id = extract_ictrl(virq);
            auto hw_irq   = extract_hwirq(virq);
            auto ctrl_res = get_controller(ictrl_id);
            if (!ctrl_res.has_value()) {
                propagate_return(ctrl_res);
            }
            return ctrl_res.value().get().set_priority(hw_irq, prio);
        }

        /**
         * @brief 设置虚拟中断亲和性.
         */
        [[nodiscard]]
        Result<void> set_affinity(virq_t virq, cpu_mask_t mask) {
            auto ictrl_id = extract_ictrl(virq);
            auto hw_irq   = extract_hwirq(virq);
            auto ctrl_res = get_controller(ictrl_id);
            if (!ctrl_res.has_value()) {
                propagate_return(ctrl_res);
            }
            return ctrl_res.value().get().set_affinity(hw_irq, mask);
        }
    };

    class ClintTimer : public ClockEvent {
    public:
        /**
         * @brief 构造绑定到指定时钟源的 CLINT 定时事件设备.
         *
         * @param clksrc 供定时器换算 tick 的时钟源
         */
        explicit ClintTimer(ClockSource *clksrc) noexcept
            : ClockEvent(clksrc) {
            _last_recorded_time = _clksrc->to_ns(_clksrc->now());
        }

        /**
         * @brief 安排下一次定时事件.
         *
         * @param delta 相对当前时刻的触发延迟
         */
        void setNextEvent(units::time delta) noexcept override;

        /**
         * @brief 获取该定时器支持的最大触发延迟.
         *
         * @return units::time 最大延迟
         */
        [[nodiscard]]
        units::time maxDelta() const noexcept override {
            return UINT64_MAX / _clksrc->frequency();
        }

        /**
         * @brief 设置定时事件到期处理函数.
         *
         * @param handler 到期时调用的回调函数
         */
        void setHandler(Handler &&handler) noexcept override
        {
            _handler = std::move(handler);
        }

        /**
         * @brief 在定时器中断到来时触发事件回调.
         */
        void onTimerIrq() noexcept;

    private:
        Handler _handler;
        units::time _last_recorded_time;
    };

    /**
     * @brief RISC-V CLINT 控制器实现.
     */
    class Clint : public IntCtrl {
    public:
        static constexpr hwirq_t S_SOFT_IRQ  = 1;
        static constexpr hwirq_t S_TIMER_IRQ = 5;

    private:
        std::string _name;
        ictrl_t _identifier;
        std::vector<PhyArea> _mmio_regions;
        cpuid_t _hart_id;
    public:
        /**
         * @brief RISC-V CLINT 控制器.
         *
         * @param clksrc 定时器使用的时钟源.
         * @param name 控制器名称.
         * @param identifier 控制器 ID.
         * @param mmio_regions MMIO 区域列表.
         * @param hart_id 该CLINT所属的hart.
         */
        Clint(std::string name, ictrl_t identifier,
              std::vector<PhyArea> mmio_regions, cpuid_t hart_id) noexcept;

        /**
         * @brief 销毁控制器对象.
         */
        ~Clint() override = default;

        /**
         * @brief 获取控制器名称.
         */
        [[nodiscard]]
        const char *name() const noexcept override;
        /**
         * @brief 获取 MMIO 区域列表.
         */
        [[nodiscard]]
        std::vector<PhyArea> mmio_regions() const override;
        /**
         * @brief 使能本地中断.
         */
        [[nodiscard]]
        Result<void> enable_irq(hwirq_t hw_irq) override;
        /**
         * @brief 屏蔽本地中断.
         */
        [[nodiscard]]
        Result<void> disable_irq(hwirq_t hw_irq) override;
        /**
         * @brief 设置中断优先级.
         */
        [[nodiscard]]
        Result<void> set_priority(hwirq_t hw_irq, irq_prio_t prio) override;
        /**
         * @brief 设置中断亲和性.
         */
        [[nodiscard]]
        Result<void> set_affinity(hwirq_t hw_irq, cpu_mask_t mask) override;

        /**
         * @brief 获取控制器 ID.
         */
        [[nodiscard]]
        ictrl_t identifier() const noexcept {
            return _identifier;
        }

        /**
         * @brief 该Clint所属的hart ID.
         */
        [[nodiscard]]
        cpuid_t hart_id() const noexcept {
            return _hart_id;
        }
    };
}  // namespace device
