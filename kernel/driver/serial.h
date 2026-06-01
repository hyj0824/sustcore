/**
 * @file serial.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 串口设备与驱动
 * @version alpha-1.0.0
 * @date 2026-05-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <device/resource.h>
#include <driver/base.h>
#include <driver/factory.h>
#include <driver/int/base.h>

#include <optional>
#include <string>
#include <vector>

namespace driver {
    /**
     * @brief 串口驱动
     */
    class SerialDevice final : public DriverBase {
    private:
        static constexpr std::string_view NS16550A_COMPATIBLE = "ns16550a";
        static constexpr std::string_view CLOCK_FREQUENCY_PROP = "clock-frequency";

    public:
        /**
         * @brief 销毁驱动.
         */
        ~SerialDevice() override = default;

        /**
         * @brief 获取 UART 输入时钟频率.
         *
         * @return units::frequency 时钟频率.
         */
        [[nodiscard]]
        units::frequency clock_frequency() const noexcept;

        /**
         * @brief 获取驱动命中的主 compatible.
         *
         * @return std::string_view compatible 字符串.
         */
        [[nodiscard]]
        std::string_view compatible() const noexcept override
        {
            return NS16550A_COMPATIBLE;
        }

        void writec(char ch);
        void write(const char *str, size_t len);

    private:
        /**
         * @brief 构造一个串口驱动.
         *
         * @param node 统一串口设备节点引用指针.
         * @param name 设备名称.
         * @param clock_frequency UART 输入时钟频率.
         */
        SerialDevice(const device::DeviceNode &node,
                     units::frequency clock_frequency) noexcept;

        using uart_t = sus_u32;
        struct NS16550 {
            union {
                uart_t rbr;
                uart_t thr;
                uart_t dll;
            };
            union {
                uart_t dlh;
                uart_t ier;
            };
            union {
                uart_t iir;
                uart_t fcr;
            };
            uart_t lcr;
            uart_t mcr;
            uart_t lsr;
            uart_t msr;
            uart_t sch;
            uart_t __rsvd1[23];
            uart_t usr;
            uart_t tfl;
            uart_t rfl;
            uart_t __rsvd2[7];
            uart_t halt;
        } __attribute__((packed));
        constexpr static size_t UART_RBR  = 0x00;
        constexpr static size_t UART_THR  = 0x00;
        constexpr static size_t UART_DLL  = 0x00;
        constexpr static size_t UART_DLH  = 0x04;
        constexpr static size_t UART_IER  = 0x04;
        constexpr static size_t UART_IIR  = 0x08;
        constexpr static size_t UART_FCR  = 0x08;
        constexpr static size_t UART_LCR  = 0x0C;
        constexpr static size_t UART_MCR  = 0x10;
        constexpr static size_t UART_LSR  = 0x14;
        constexpr static size_t UART_MSR  = 0x18;
        constexpr static size_t UART_SCH  = 0x1C;
        constexpr static size_t UART_USR  = 0x7C;
        constexpr static size_t UART_TFL  = 0x80;
        constexpr static size_t UART_RFL  = 0x84;
        constexpr static size_t UART_HALT = 0xA4;
        constexpr static size_t UART_REG_SIZE = 0xA8;

        static_assert(offsetof(NS16550, rbr) == UART_RBR);
        static_assert(offsetof(NS16550, thr) == UART_THR);
        static_assert(offsetof(NS16550, dll) == UART_DLL);
        static_assert(offsetof(NS16550, dlh) == UART_DLH);
        static_assert(offsetof(NS16550, ier) == UART_IER);
        static_assert(offsetof(NS16550, iir) == UART_IIR);
        static_assert(offsetof(NS16550, fcr) == UART_FCR);
        static_assert(offsetof(NS16550, lcr) == UART_LCR);
        static_assert(offsetof(NS16550, mcr) == UART_MCR);
        static_assert(offsetof(NS16550, lsr) == UART_LSR);
        static_assert(offsetof(NS16550, msr) == UART_MSR);
        static_assert(offsetof(NS16550, sch) == UART_SCH);
        static_assert(offsetof(NS16550, usr) == UART_USR);
        static_assert(offsetof(NS16550, tfl) == UART_TFL);
        static_assert(offsetof(NS16550, rfl) == UART_RFL);
        static_assert(offsetof(NS16550, halt) == UART_HALT);

        static_assert(sizeof(NS16550) == UART_REG_SIZE);

        units::frequency _clock_frequency = units::frequency::from_hz(0);
        volatile NS16550 *uart = nullptr;

        friend class SerialDeviceFactory;
    };

    /**
     * @brief FDT 串口普通设备工厂.
     */
    class SerialDeviceFactory final : public IDeviceFactory {
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
