/**
 * @file ls7a.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief LS7A RTC 设备驱动头文件
 * @version alpha-1.0.0
 * @date 2026-06-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <driver/base.h>
#include <driver/factory.h>
#include <driver/rtc/rtc.h>
#include <syscall/uaccess.h>
#include <vfs/device.h>
#include <sus/types.h>
#include <sus/units.h>

#include <functional>
#include <string_view>

namespace driver {
    /**
     * @brief Loongson LS7A RTC 驱动.
     *
     * 当前仅实现实时时间读取能力, 不提供 alarm 编程与中断处理.
     */
    class LS7ARTC final : public DriverBase, public rtc::IRTC {
    private:
        static constexpr std::string_view LS7A_RTC_COMPATIBLE =
            "loongson,ls7a-rtc";

    public:
        using AlarmHandler = std::function<void(units::time)>;
        struct rtc_tm {
            int tm_sec;
            int tm_min;
            int tm_hour;
            int tm_mday;
            int tm_mon;
            int tm_year;
            int tm_wday;
            int tm_yday;
            int tm_isdst;
        };
        static constexpr size_t RTC_RD_TIME = 0x80247009ULL;

        ~LS7ARTC() override;

        [[nodiscard]]
        std::string_view compatible() const noexcept override {
            return LS7A_RTC_COMPATIBLE;
        }

        [[nodiscard]]
        units::time read_time() noexcept override;

        /**
         * @brief LS7A RTC 当前未实现 alarm 编程.
         *
         * @param when 目标时间.
         * @param handler 到期处理函数.
         */
        void set_alarm(units::time when, AlarmHandler handler) noexcept;

        [[nodiscard]]
        Result<void> mount(CapIdx devdir) noexcept override;

        Result<void> ioctl(size_t cmd, syscall::UBuffer &&arg) noexcept;

    private:
        explicit LS7ARTC(DevRes res, char *base) noexcept;

        using rtc_t                             = sus_u32;
        constexpr static size_t TOY_TRIM        = 0x20;
        constexpr static size_t TOY_WRITE0      = 0x24;
        constexpr static size_t TOY_WRITE1      = 0x28;
        constexpr static size_t TOY_READ0       = 0x2C;
        constexpr static size_t TOY_READ1       = 0x30;
        constexpr static size_t TOY_MATCH0      = 0x34;
        constexpr static size_t TOY_MATCH1      = 0x38;
        constexpr static size_t TOY_MATCH2      = 0x3C;
        constexpr static size_t RTC_CTRL        = 0x40;
        constexpr static size_t RTC_TRIM        = 0x60;
        constexpr static size_t RTC_WRITE0      = 0x64;
        constexpr static size_t RTC_READ0       = 0x68;
        constexpr static size_t RTC_MATCH0      = 0x6C;
        constexpr static size_t RTC_MATCH1      = 0x70;
        constexpr static size_t RTC_MATCH2      = 0x74;
        constexpr static size_t RTC_REG_SIZE    = 0x100;

        struct ReadRegs {
            rtc_t reserved0[TOY_READ0 / sizeof(rtc_t)];
            rtc_t toy_read0;
            rtc_t toy_read1;
        };

        static_assert(offsetof(ReadRegs, toy_read0) == TOY_READ0,
                      "toy_read0 offset mismatch!");
        static_assert(offsetof(ReadRegs, toy_read1) == TOY_READ1,
                      "toy_read1 offset mismatch!");

        volatile ReadRegs *regs = nullptr;

        friend class LS7ARTCFactory;
    };

    /**
     * @brief LS7A RTC FDT 设备工厂.
     */
    class LS7ARTCFactory final : public IDeviceFactory {
    public:
        [[nodiscard]]
        const DeviceId &device_id() const noexcept override;

        [[nodiscard]]
        Result<DriverBase *> create(const device::DeviceNode &node,
                                    device::DeviceModel &model,
                                    b64 driver_flag) const override;
    };
}  // namespace driver
