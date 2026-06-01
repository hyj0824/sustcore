/**
 * @file goldfish.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief goldfish RTC 设备驱动
 * @version alpha-1.0.0
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <driver/rtc/goldfish.h>

namespace driver {
    GoldfishRTC::GoldfishRTC(const device::DeviceNode &node) noexcept
        : DriverBase(node) {
        assert(_mmios.size() >= 1);
        auto *mmio = _mmios[0].get();
        assert(mmio != nullptr);
        assert(mmio->region().size() >= sizeof(Goldfish));
        auto map_res = device::MMIOManager::inst().map_to_kernel(*mmio);
        assert(map_res.has_value());
        goldfish = map_res.value().as<Goldfish>();
    }

    [[nodiscard]]
    units::time GoldfishRTC::read_time() const noexcept
    {
        sus_u64 time_high = goldfish->time_high;
        sus_u64 time_low  = goldfish->time_low;
        sus_u64 time = (time_high & 0xFFFFFFFF) << 32 | (time_low & 0xFFFFFFFF);
        return units::time::from_seconds(time);
    }

    /**
     * @brief 获取该工厂支持的主 compatible.
     *
     * @return std::string_view compatible 字符串.
     */
    [[nodiscard]]
    std::string_view GoldfishRTCFactory::compatible() const noexcept {
        return GoldfishRTC::GOLDFISH_RTC_COMPATIBLE;
    }

    /**
     * @brief 基于统一设备节点创建串口设备包装对象.
     *
     * @param node 统一设备节点.
     * @param model 设备模型.
     * @return Result<DriverBase*> 创建结果.
     */
    [[nodiscard]]
    Result<DriverBase *> GoldfishRTCFactory::create(
        const device::DeviceNode &node, device::DeviceModel &model) const {
        (void)model;
        return new GoldfishRTC(node);
    }
}  // namespace driver