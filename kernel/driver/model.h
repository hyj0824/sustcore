/**
 * @file model.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 驱动模型
 * @version alpha-1.0.0
 * @date 2026-05-31
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <device/device.h>
#include <driver/base.h>
#include <driver/factory.h>
#include <sus/owner.h>

#include <vector>

namespace device {
    class DeviceModel;
}

namespace driver {
    /**
     * @brief 驱动模型.
     *
     * 统一掌管驱动工厂注册、工厂对象生命周期和运行时驱动对象生命周期.
     */
    class DriverModel {
    public:
        DriverModel(const DriverModel &)            = delete;
        DriverModel &operator=(const DriverModel &) = delete;
        DriverModel(DriverModel &&)                 = delete;
        DriverModel &operator=(DriverModel &&)      = delete;

        /**
         * @brief 销毁驱动模型.
         */
        ~DriverModel() {
            cleanup();
        }

        /**
         * @brief 获取驱动模型单例.
         *
         * @return DriverModel& 单例引用.
         */
        [[nodiscard]]
        static DriverModel &inst() noexcept;

        /**
         * @brief 初始化驱动模型单例.
         */
        static void init() noexcept;

        /**
         * @brief 判断驱动模型是否已初始化.
         *
         * @return bool 是否已初始化.
         */
        [[nodiscard]]
        static bool initialized() noexcept;

        /**
         * @brief 清理全部工厂对象与运行时驱动对象.
         */
        void cleanup() noexcept;

        /**
         * @brief 注册普通设备工厂并接管其所有权.
         *
         * @param factory 工厂对象 owner.
         * @return Result<void> 注册结果.
         */
        [[nodiscard]]
        Result<void> register_factory(
            util::owner<IDeviceFactory *> factory) noexcept;

        /**
         * @brief 注册 IRQ 设备工厂并接管其所有权.
         *
         * @param factory 工厂对象 owner.
         * @return Result<void> 注册结果.
         */
        [[nodiscard]]
        Result<void> register_factory(
            util::owner<IIrqChipFactory *> factory) noexcept;

        /**
         * @brief 基于指定设备节点创建匹配驱动并接管其生命周期.
         *
         * @param node 统一设备节点非拥有指针.
         * @param model 设备模型.
         * @return Result<DriverBase*> 创建成功的驱动非拥有指针.
         */
        [[nodiscard]]
        Result<DriverBase *> create_driver(device::DeviceNode *node) noexcept;

        /**
         * @brief 获取普通设备工厂注册表只读引用.
         *
         * @return const DeviceFactoryRegistry& 普通工厂注册表.
         */
        [[nodiscard]]
        const DeviceFactoryRegistry &device_factories() const noexcept {
            return _device_factories;
        }

        /**
         * @brief 获取 IRQ 工厂注册表只读引用.
         *
         * @return const IrqChipFactoryRegistry& IRQ 工厂注册表.
         */
        [[nodiscard]]
        const IrqChipFactoryRegistry &irq_factories() const noexcept {
            return _irq_factories;
        }

    private:
        DriverModel() = default;

        static DriverModel _INSTANCE;
        static bool _initialized;

        DeviceFactoryRegistry _device_factories;
        IrqChipFactoryRegistry _irq_factories;
        std::vector<util::owner<IDeviceFactory *>> _owned_device_factories;
        std::vector<util::owner<IIrqChipFactory *>> _owned_irq_factories;
        std::vector<util::owner<DriverBase *>> _drivers;
    };
}  // namespace driver
