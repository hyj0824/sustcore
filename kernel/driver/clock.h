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
#include <sustcore/errcode.h>
#include <sustcore/epacks.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace driver {
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
     * @brief 一次时钟中断事件的时间上下文.
     */
    struct ClockEvent {
    public:
        units::time last{};
        units::time now{};
    };

    /**
     * @brief 可编程下一次中断的硬件闹钟抽象接口.
     */
    class Alarm {
    public:
        using Handler = std::function<void(ClockEvent)>;

        /**
         * @brief 构造一个绑定到指定时钟源的时钟事件设备.
         *
         * @param clksrc 事件依赖的底层时钟源
         */
        constexpr explicit Alarm(ClockSource *clksrc) noexcept
            : _clksrc(clksrc) {}
        virtual ~Alarm() = default;

        /**
         * @brief 该函数会编程硬件时钟事件设备,
         * 使其在相对于"现在"的 delta 时间后产生中断.
         *
         * @param delta 事件触发的时间间隔
         */
        virtual void set_next_event(units::time delta) = 0;
        /**
         * @brief 获得该时钟事件设备能够支持的最大时间间隔.
         *
         * @return units::time 最大时间间隔
         */
        [[nodiscard]]
        virtual units::time max_delta() const       = 0;
        /**
         * @brief 注册时钟事件到期时的回调处理函数
         *
         * @param handler 事件处理函数
         */
        virtual void set_handler(Handler &&handler) = 0;

        /**
         * @brief 获取该 Alarm 绑定的时钟源.
         *
         * @return ClockSource* 底层时钟源.
         */
        [[nodiscard]]
        constexpr ClockSource *clock_source() const noexcept {
            return _clksrc;
        }

    protected:
        ClockSource *_clksrc = nullptr;
    };

    /**
     * @brief 到期动作类型常量.
     */
    namespace expact {
        constexpr size_t SCHD   = 1;
        constexpr size_t WAKEUP = 2;
        constexpr size_t TIMEOUT = 3;

        namespace schd {
            constexpr size_t TICK               = 1;
            constexpr size_t TIMEKEEPER_LOGTEST = 2;
        }  // namespace schd
    }  // namespace expact

    /**
     * @brief 一个结构化的到期动作.
     */
    struct ExpireAction {
        size_t expireTime   = 0;
        size_t expireAction = 0;
        size_t expireArg0   = 0;
        size_t expireArg1   = 0;
    };

    /**
     * @brief 队列中动作的稳定句柄.
     */
    struct ExpireHandle {
        static constexpr uint16_t INVALID_SLOT = UINT16_MAX;

        uint16_t slot       = INVALID_SLOT;
        uint16_t generation = 0;

        [[nodiscard]]
        constexpr bool valid() const noexcept {
            return slot != INVALID_SLOT;
        }

        constexpr void reset() noexcept {
            slot       = INVALID_SLOT;
            generation = 0;
        }
    };

    /// pop_due 一次最多弹出的到期动作数（避免 ISR 内分配堆内存）
    constexpr size_t MAX_DUE_ACTIONS = 8;

    /**
     * @brief pop_due 的返回结果 —— 固定大小数组，不在 ISR 内分配堆内存.
     */
    struct PopDueResult {
        ExpireAction entries[MAX_DUE_ACTIONS]{};
        size_t count = 0;
    };

    /**
     * @brief 保存到期动作的小根堆.
     */
    class ExpireActionQueue {
    public:
        /**
         * @brief 判断队列是否为空.
         *
         * @return true 队列为空.
         * @return false 队列非空.
         */
        [[nodiscard]]
        bool empty() const noexcept;

        /**
         * @brief 获取队列长度.
         *
         * @return size_t 当前队列中的动作数.
         */
        [[nodiscard]]
        size_t size() const noexcept;

        /**
         * @brief 插入一个到期动作.
         *
         * @param action 待插入动作.
         * @return Result<ExpireHandle> 插入后可用于取消的稳定句柄.
         */
        [[nodiscard]]
        Result<ExpireHandle> push(const ExpireAction &action) noexcept;

        /**
         * @brief 根据句柄取消一个动作并真正移出队列.
         *
         * @param handle 待取消动作句柄.
         * @return true 成功移除.
         * @return false 句柄无效或动作已不存在.
         */
        [[nodiscard]]
        bool cancel(ExpireHandle handle) noexcept;

        /**
         * @brief 查看当前最早到期动作.
         *
         * @return std::optional<ExpireAction> 最早到期动作.
         */
        [[nodiscard]]
        std::optional<ExpireAction> peek() const noexcept;

        /**
         * @brief 弹出所有到期时间不晚于 now_ns 的动作.
         *
         * @param now_ns 当前时间（纳秒）.
         * @return PopDueResult 已到期动作集合.
         */
        [[nodiscard]]
        PopDueResult pop_due(size_t now_ns) noexcept;

    private:
        static constexpr size_t MAX_HEAP_ACTIONS = 32;

        struct ExpireSlot {
            ExpireAction action{};
            uint16_t heap_index = 0;
            uint16_t next_free  = ExpireHandle::INVALID_SLOT;
            uint16_t generation = 1;
            bool active         = false;
        };

    public:
        ExpireActionQueue() noexcept;

    private:
        /**
         * @brief 删除堆中指定位置的动作.
         *
         * @param heap_index 目标堆下标.
         * @return ExpireAction 被移除的动作.
         */
        [[nodiscard]]
        ExpireAction remove_at(size_t heap_index) noexcept;

        /**
         * @brief 将指定节点向上调整以恢复小根堆性质.
         *
         * @param index 新插入节点下标.
         */
        void shift_up(size_t index) noexcept;

        /**
         * @brief 将指定节点向下调整以恢复小根堆性质.
         *
         * @param index 根节点下标.
         */
        void shift_down(size_t index) noexcept;

        /**
         * @brief 交换两个堆节点并同步槽位中的 heap_index.
         *
         * @param lhs 左节点下标.
         * @param rhs 右节点下标.
         */
        void swap_heap_nodes(size_t lhs, size_t rhs) noexcept;

        /**
         * @brief 比较两个槽位动作的到期时间.
         *
         * @param lhs 左槽位编号.
         * @param rhs 右槽位编号.
         * @return true 左动作更晚到期.
         * @return false 左动作不晚于右动作.
         */
        [[nodiscard]]
        bool later(uint16_t lhs, uint16_t rhs) const noexcept;

        uint16_t _heap[MAX_HEAP_ACTIONS]{};
        ExpireSlot _slots[MAX_HEAP_ACTIONS]{};
        uint16_t _free_head = ExpireHandle::INVALID_SLOT;
        size_t _heap_size   = 0;
    };

    /**
     * @brief 当前 hart 的到期事件管理器.
     */
    class TimeKeeper {
    public:
        /**
         * @brief 使用当前 hart 的时钟源与 Alarm 构造 TimeKeeper.
         *
         * @param source 当前 hart 的 ClockSource.
         * @param alarm 当前 hart 的 Alarm.
         */
        TimeKeeper(ClockSource *source, Alarm *alarm) noexcept;

        /**
         * @brief 新增一个到期动作.
         *
         * @param action 待提交动作.
         * @return Result<ExpireHandle> 若动作入队成功则返回取消句柄.
         */
        [[nodiscard]]
        Result<ExpireHandle> enqueue(const ExpireAction &action) noexcept;

        /**
         * @brief 取消一个仍在队列中的到期动作.
         *
         * @param handle 待取消动作句柄.
         * @return true 成功从队列中移除.
         * @return false 句柄无效或动作已不在队列中.
         */
        [[nodiscard]]
        bool cancel(ExpireHandle handle) noexcept;

        /**
         * @brief 处理一次 Alarm 到期中断.
         *
         * @param event 本次中断事件信息.
         */
        void on_timer_irq(const ClockEvent &event) noexcept;

        /**
         * @brief 获取底层时钟源.
         *
         * @return ClockSource* 底层时钟源.
         */
        [[nodiscard]]
        constexpr ClockSource *source() const noexcept {
            return _source;
        }

        /**
         * @brief 获取底层 Alarm.
         *
         * @return Alarm* 当前绑定的 Alarm.
         */
        [[nodiscard]]
        constexpr Alarm *alarm() const noexcept {
            return _alarm;
        }

    private:
        /**
         * @brief 根据当前队列根重新编程下一次 timer.
         */
        void rearm_timer() noexcept;

        /**
         * @brief 在已关闭本地中断的上下文中重编程下一次 timer.
         */
        void rearm_timer_locked() noexcept;

        /**
         * @brief 执行一个到期动作.
         *
         * @param action 待执行动作.
         * @param event 本次时钟事件信息.
         */
        void run_action(const ExpireAction &action,
                        const ClockEvent &event) noexcept;

        ClockSource *_source = nullptr;
        Alarm *_alarm = nullptr;
        ExpireActionQueue _queue{};
    };

}  // namespace driver
