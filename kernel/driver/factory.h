/**
 * @file factory.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief compatible 工厂注册表
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <device/device.h>
#include <driver/base.h>
#include <sustcore/errcode.h>

#include <string_view>
#include <unordered_map>

namespace device
{
    class DeviceModel;
}

namespace driver {

    /**
     * @brief 普通设备工厂接口.
     */
    class IDeviceFactory {
    public:
        virtual ~IDeviceFactory() = default;

        /**
         * @brief 获取该工厂服务的主 compatible.
         *
         * @return std::string_view compatible 字符串.
         */
        [[nodiscard]]
        virtual std::string_view compatible() const noexcept = 0;

        /**
         * @brief 在 compatible 命中后再次裁决是否由当前工厂接管该节点.
         *
         * 默认实现直接接受, 便于现有工厂保持兼容.
         *
         * @param node 统一设备节点.
         * @param model 设备模型.
         * @return bool 是否接受该节点.
         */
        [[nodiscard]]
        virtual bool probe(const device::DeviceNode &node,
                           device::DeviceModel &model) const noexcept;

        /**
         * @brief 基于统一设备节点创建 driver 设备包装对象.
         *
         * @param node 统一设备节点.
         * @param model 设备模型.
         * @return Result<DriverBase*> 创建结果.
         */
        [[nodiscard]]
        virtual Result<DriverBase *> create(
            const device::DeviceNode &node, device::DeviceModel &model) const = 0;
    };

    /**
     * @brief IRQ 设备工厂接口.
     */
    class IIrqChipFactory {
    public:
        virtual ~IIrqChipFactory() = default;

        /**
         * @brief 获取该 IRQ 工厂服务的主 compatible.
         *
         * @return std::string_view compatible 字符串.
         */
        [[nodiscard]]
        virtual std::string_view compatible() const noexcept = 0;

        /**
         * @brief 基于统一设备节点创建并接入最终 IRQ 运行时对象.
         *
         * @param node 统一设备节点.
         * @param model 设备模型.
         * @return Result<DriverBase*> 创建结果.
         */
        [[nodiscard]]
        virtual Result<DriverBase *> create(
            const device::DeviceNode &node, device::DeviceModel &model) const = 0;
    };

    /**
     * @brief 按 compatible 索引的普通设备工厂注册表.
     */
    class DeviceFactoryRegistry {
    public:
        [[nodiscard]]
        Result<void> register_factory(const IDeviceFactory &factory) noexcept;
        [[nodiscard]]
        const IDeviceFactory *find(const device::DeviceNode &node) const noexcept;

    private:
        std::unordered_map<std::string_view, const IDeviceFactory *> _factories;
    };

    /**
     * @brief 按 compatible 索引的 IRQ 工厂注册表.
     */
    class IrqChipFactoryRegistry {
    public:
        [[nodiscard]]
        Result<void> register_factory(const IIrqChipFactory &factory) noexcept;
        [[nodiscard]]
        const IIrqChipFactory *find(const device::DeviceNode &node) const noexcept;

    private:
        std::unordered_map<std::string_view, const IIrqChipFactory *> _factories;
    };
}  // namespace device
