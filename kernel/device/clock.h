/**
 * @file clock.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 时钟
 * @version alpha-1.0.0
 * @date 2026-05-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sbi/sbi.h>
#include <sus/units.h>
#include <sustcore/epacks.h>

#include <functional>
namespace device {
    /**
     * @brief 系统时钟源抽象接口.
     */
    class ClockSource {
    public:
        virtual ~ClockSource() = default;

        /**
         * @brief 获取当前时钟计数值.
         *
         * @return units::tick 当前 tick 计数
         */
        [[nodiscard]]
        virtual units::tick now() const = 0;

        /**
         * @brief 获取时钟源频率.
         *
         * @return units::frequency 时钟频率
         */
        [[nodiscard]]
        virtual units::frequency frequency() const = 0;

        /**
         * @brief 将 tick 计数换算为时间值.
         *
         * @param ticks 要换算的 tick 计数
         * @return units::time 换算后的时间值
         */
        [[nodiscard]]
        units::time to_ns(units::tick ticks) const noexcept {
            return ticks / frequency();
        }
    };

    /**
     * @brief 时钟事件回调上下文.
     */
    struct ClockEventInfo {
        units::time last;
        units::time now;
    };

    /**
     * @brief 可编程时钟事件设备抽象接口.
     */
    class ClockEvent {
    public:
        using Handler = std::function<void(ClockEventInfo)>;

        /**
         * @brief 构造一个绑定到指定时钟源的时钟事件设备.
         *
         * @param clksrc 事件依赖的底层时钟源
         */
        constexpr explicit ClockEvent(ClockSource *clksrc) noexcept
            : _clksrc(clksrc) {}
        virtual ~ClockEvent() = default;

        /**
         * @brief 该函数会编程硬件时钟事件设备,
         * 使其在相对于"现在"的 delta 时间后产生中断.
         *
         * @param delta 事件触发的时间间隔
         */
        virtual void setNextEvent(units::time delta) = 0;
        /**
         * @brief 获得该时钟事件设备能够支持的最大时间间隔.
         *
         * @return units::time 最大时间间隔
         */
        [[nodiscard]]
        virtual units::time maxDelta() const       = 0;
        /**
         * @brief 注册时钟事件到期时的回调处理函数
         *
         * @param handler 事件处理函数
         */
        virtual void setHandler(Handler &&handler) = 0;

    protected:
        ClockSource *_clksrc = nullptr;
    };

    /**
     * @brief 基于 RISC-V `time` CSR 的系统时钟源.
     */
    class CSRTimeClockSource : public ClockSource {
    public:
        /**
         * @brief 构造 CSR `time` 时钟源.
         *
         * @param freq 时钟源频率
         */
        explicit CSRTimeClockSource(units::frequency freq) noexcept
            : _freq(freq) {}

        /**
         * @brief 读取当前 `time` CSR 计数.
         *
         * @return units::tick 当前 tick 计数
         */
        [[nodiscard]]
        units::tick now() const noexcept override;

        /**
         * @brief 获取 CSR `time` 时钟源频率.
         *
         * @return units::frequency 时钟频率
         */
        [[nodiscard]]
        units::frequency frequency() const noexcept override;

    private:
        units::frequency _freq;
    };
}  // namespace device
