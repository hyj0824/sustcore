/**
 * @file goldfish.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief goldfish RTC 设备驱动头文件
 * @version alpha-1.0.0
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <driver/base.h>
#include <driver/factory.h>
#include <sus/units.h>
#include <string_view>
#include <sus/types.h>

namespace driver {
    /**
     * @brief 串口驱动
     */
    class GoldfishRTC final : public DriverBase {
    private:
        static constexpr std::string_view GOLDFISH_RTC_COMPATIBLE = "google,goldfish-rtc";
    public:
        /**
         * @brief 销毁驱动.
         */
        ~GoldfishRTC() override = default;

        /**
         * @brief 获取设备命中的主 compatible.
         *
         * @return std::string_view compatible 字符串.
         */
        [[nodiscard]]
        std::string_view compatible() const noexcept override
        {
            return GOLDFISH_RTC_COMPATIBLE;
        }

        [[nodiscard]]
        units::time read_time() const noexcept;
    private:
        /**
         * @brief 构造一个串口驱动.
         *
         * @param node 统一串口设备节点引用指针.
         * @param name 设备名称.
         */
        GoldfishRTC(const device::DeviceNode &node) noexcept;

        using rtc_t = sus_u32;
        struct Goldfish {
            rtc_t time_low;
            rtc_t time_high;
            rtc_t alarm_low;
            rtc_t alarm_high;
            rtc_t irq_enable;
            rtc_t int_flag;
        } __attribute__((packed));
        constexpr static size_t RTC_TIME_LOW = 0x00;
        constexpr static size_t RTC_TIME_HIGH = 0x04;
        constexpr static size_t RTC_ALARM_LOW = 0x08;
        constexpr static size_t RTC_ALARM_HIGH = 0x0C;
        constexpr static size_t RTC_IRQ_ENABLE = 0x10;
        constexpr static size_t RTC_INT_FLAG = 0x14;
        constexpr static size_t RTC_REG_SIZE = 0x18;

        static_assert(offsetof(Goldfish, time_low) == RTC_TIME_LOW);
        static_assert(offsetof(Goldfish, time_high) == RTC_TIME_HIGH);
        static_assert(offsetof(Goldfish, alarm_low) == RTC_ALARM_LOW);
        static_assert(offsetof(Goldfish, alarm_high) == RTC_ALARM_HIGH);
        static_assert(offsetof(Goldfish, irq_enable) == RTC_IRQ_ENABLE);
        static_assert(offsetof(Goldfish, int_flag) == RTC_INT_FLAG);

        static_assert(sizeof(Goldfish) == RTC_REG_SIZE);

        volatile Goldfish *goldfish = nullptr;

        friend class GoldfishRTCFactory;
    };

    /**
     * @brief FDT 串口普通设备工厂.
     */
    class GoldfishRTCFactory final : public IDeviceFactory {
    public:
        /**
         * @brief 获取该工厂支持的主 compatible.
         *
         * @return std::string_view compatible 字符串.
         */
        [[nodiscard]]
        std::string_view compatible() const noexcept override;

        /**
         * @brief 基于统一设备节点创建串口设备包装对象.
         *
         * @param node 统一设备节点.
         * @param model 设备模型.
         * @return Result<DriverBase*> 创建结果.
         */
        [[nodiscard]]
        Result<DriverBase *> create(const device::DeviceNode &node,
                                    device::DeviceModel &model) const override;
    };
}  // namespace driver