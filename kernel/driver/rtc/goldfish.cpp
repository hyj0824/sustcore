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
#include <mem/userspace.h>

namespace driver {
    GoldfishRTC::GoldfishRTC(ResPack res, char *base) noexcept
        : DriverBase(std::move(res)),
          regs(reinterpret_cast<volatile Goldfish *>(base))
    {
        // to flush the rtc registers
        [[maybe_unused]] auto t = read_time();
    }

    [[nodiscard]]
    units::time GoldfishRTC::read_time() const noexcept {
        sus_u64 time = (static_cast<sus_u64>(regs->time_high) << 32) | regs->time_low;
        return units::time::from_nanoseconds(time);
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
        loggers::DEVICE::INFO("开始创建设备驱动: goldfish-rtc name=%s",
                              node.name());
        // 获取 mmio & virqs
        auto virqs = device::DevResManager::get_virq_resource(node);
        auto mmios = device::DevResManager::get_mmio_resource(node);

        // 以第一个 mmio 作为 RTC 寄存器基址
        if (mmios.empty()) {
            loggers::DEVICE::ERROR("Goldfish RTC 设备缺少 MMIO 资源!");
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        // 校验 mmio
        auto *mmio = mmios[0].get();
        assert(mmio != nullptr);
        assert(mmio->region().size() >= GoldfishRTC::RTC_REG_SIZE);

        // 映射
        auto map_res = device::MMIOManager::inst().map_to_kernel(*mmio);
        if (!map_res.has_value()) {
            loggers::DEVICE::ERROR("Goldfish RTC MMIO 资源映射失败!");
            unexpect_return(map_res.error());
        }
        char *base = map_res.value().as<char>();
        return new GoldfishRTC(
            DriverBase::ResPack(node, std::move(virqs), std::move(mmios)),
            base);
    }
}  // namespace driver
