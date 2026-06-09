/**
 * @file factory.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief compatible 工厂注册表
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <driver/factory.h>
#include <logger.h>

#include <device/model.h>
#include <type_traits>

namespace {
    template <typename FactoryT>
    [[nodiscard]]
    const FactoryT *find_best_factory(
        const std::unordered_map<std::string_view, const FactoryT *> &factories,
        const device::DeviceNode &node,
        device::DeviceModel *model = nullptr) noexcept {
        // foreach the compatibles nodes
        for (const auto &compatible : node.compatibles()) {
            // try to find a factory for the compatible
            auto it = factories.find(compatible);
            if (it != factories.end()) {
                loggers::DEVICE::DEBUG("找到匹配的工厂: compatible=%s",
                                       std::string(compatible).c_str());
                if constexpr (std::is_same_v<FactoryT, driver::IDeviceFactory>) {
                    if (model == nullptr) {
                        loggers::DEVICE::WARN(
                            "普通设备工厂二次探测缺少 DeviceModel: compatible=%s",
                            std::string(compatible).c_str());
                        continue;
                    }
                    if (!it->second->probe(node, *model)) {
                        loggers::DEVICE::DEBUG(
                            "工厂 probe 拒绝接管节点: compatible=%s",
                            std::string(compatible).c_str());
                        continue;
                    }
                    loggers::DEVICE::DEBUG(
                        "工厂 probe 接受接管节点: compatible=%s",
                        std::string(compatible).c_str());
                }
                return it->second;
            }
        }
        return nullptr;
    }
}  // namespace

namespace driver {
    bool IDeviceFactory::probe(const device::DeviceNode &node,
                               device::DeviceModel &model) const noexcept {
        (void)node;
        (void)model;
        return true;
    }

    /**
     * @brief 向普通设备工厂注册表登记工厂.
     */
    Result<void> DeviceFactoryRegistry::register_factory(
        const IDeviceFactory &factory) noexcept {
        auto compatible = factory.compatible();
        if (_factories.contains(compatible)) {
            loggers::DEVICE::ERROR("DeviceFactory 重复注册: %s",
                                   std::string(compatible).c_str());
            unexpect_return(ErrCode::KEY_DUPLICATED);
        }
        _factories.emplace(compatible, &factory);
        loggers::DEVICE::DEBUG("注册 DeviceFactory: %s",
                               std::string(compatible).c_str());
        void_return();
    }

    /**
     * @brief 按节点 compatible 查找最佳普通设备工厂.
     */
    const IDeviceFactory *DeviceFactoryRegistry::find(
        const device::DeviceNode &node) const noexcept {
        auto *model =
            device::DeviceModel::initialized() ? &device::DeviceModel::inst()
                                               : nullptr;
        return find_best_factory(_factories, node, model);
    }

    /**
     * @brief 向 IRQ 工厂注册表登记工厂.
     */
    Result<void> IrqChipFactoryRegistry::register_factory(
        const IIrqChipFactory &factory) noexcept {
        auto compatible = factory.compatible();
        if (_factories.contains(compatible)) {
            loggers::DEVICE::ERROR("IrqChipFactory 重复注册: %s",
                                   std::string(compatible).c_str());
            unexpect_return(ErrCode::KEY_DUPLICATED);
        }
        _factories.emplace(compatible, &factory);
        loggers::DEVICE::DEBUG("注册 IrqChipFactory: %s",
                               std::string(compatible).c_str());
        void_return();
    }

    /**
     * @brief 按节点 compatible 查找最佳 IRQ 工厂.
     */
    const IIrqChipFactory *IrqChipFactoryRegistry::find(
        const device::DeviceNode &node) const noexcept {
        return find_best_factory(_factories, node);
    }
}  // namespace device
