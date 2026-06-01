/**
 * @file model.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 驱动模型
 * @version alpha-1.0.0
 * @date 2026-05-31
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include <device/model.h>
#include <driver/model.h>
#include <logger.h>

namespace driver {
    DriverModel DriverModel::_INSTANCE;
    bool DriverModel::_initialized = false;

    DriverModel &DriverModel::inst() noexcept {
        assert(_initialized);
        return _INSTANCE;
    }

    void DriverModel::init() noexcept {
        new (&_INSTANCE) DriverModel();
        _initialized = true;
    }

    bool DriverModel::initialized() noexcept {
        return _initialized;
    }

    void DriverModel::cleanup() noexcept {
        for (auto &driver : _drivers) {
            delete driver.get();
        }
        _drivers.clear();

        for (auto &factory : _owned_irq_factories) {
            delete factory.get();
        }
        _owned_irq_factories.clear();

        for (auto &factory : _owned_device_factories) {
            delete factory.get();
        }
        _owned_device_factories.clear();
    }

    Result<void> DriverModel::register_factory(
        util::owner<IDeviceFactory *> factory) noexcept {
        if (factory.get() == nullptr) {
            loggers::DEVICE::ERROR("注册 DeviceFactory 失败: factory 为空");
            unexpect_return(ErrCode::NULLPTR);
        }
        auto *raw = factory.get();
        auto register_res = _device_factories.register_factory(*raw);
        propagate(register_res);
        _owned_device_factories.push_back(std::move(factory));
        void_return();
    }

    Result<void> DriverModel::register_factory(
        util::owner<IIrqChipFactory *> factory) noexcept {
        if (factory.get() == nullptr) {
            loggers::DEVICE::ERROR("注册 IrqChipFactory 失败: factory 为空");
            unexpect_return(ErrCode::NULLPTR);
        }
        auto *raw = factory.get();
        auto register_res = _irq_factories.register_factory(*raw);
        propagate(register_res);
        _owned_irq_factories.push_back(std::move(factory));
        void_return();
    }

    Result<DriverBase *> DriverModel::create_driver(
        device::DeviceNode *node) noexcept {
        if (node == nullptr) {
            loggers::DEVICE::ERROR("创建驱动失败: node 为空");
            unexpect_return(ErrCode::NULLPTR);
        }

        Result<DriverBase *> driver_res = std::unexpected(ErrCode::ENTRY_NOT_FOUND);
        if (auto *irq_factory = _irq_factories.find(*node);
            irq_factory != nullptr)
        {
            driver_res = irq_factory->create(*node, device::DeviceModel::inst());
        } else if (auto *device_factory = _device_factories.find(*node);
                   device_factory != nullptr)
        {
            driver_res = device_factory->create(*node, device::DeviceModel::inst());
        } else {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        propagate(driver_res);

        if (driver_res.value() == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        _drivers.push_back(util::owner<DriverBase *>(driver_res.value()));
        return driver_res.value();
    }
}  // namespace driver
