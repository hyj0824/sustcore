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
#include <logger.h>
#include <mem/userspace.h>
#include <sustcore/errcode.h>

namespace driver {
    GoldfishRTC::GoldfishRTC(DevRes res, char *base, virq_t virq) noexcept
        : DriverBase(std::move(res)),
          regs(reinterpret_cast<volatile Goldfish *>(base))
    {
        // res shouldn't be used after this point cuz it has been moved to the base class
        assert (_mmios.size() >= 1);
        assert (_virqs.size() >= 1);

        // to flush the rtc registers
        [[maybe_unused]] auto t = read_time();

        auto register_res = _virqs[0]->register_handler([](const IrqEvent &e) {
            loggers::SUSTCORE::INFO("DEBUG MESSAGE HERE!");
        });
        assert (register_res.has_value());

        // set the alarm to current time + 5s
        t = read_time();
        const auto alarm_time = t + units::time::from_seconds(5);
        const auto formatted_alarm = units::rt_time::from_seconds(alarm_time.to_seconds()).to_formatted_time();

        loggers::SUSTCORE::INFO("设置闹钟时间: %04u-%02u-%02u %02u:%02u:%02u",
                                  formatted_alarm.year, formatted_alarm.month,
                                  formatted_alarm.day, formatted_alarm.hour,
                                  formatted_alarm.minute, formatted_alarm.second);
        sus_u64 nanos = alarm_time.to_nanoseconds();
        sus_u32 nanos_hi = static_cast<sus_u32>(nanos >> 32);
        sus_u32 nanos_lo = static_cast<sus_u32>(nanos & 0xFFFFFFFF);
        _virqs[0]->set_priority(7);

        regs->irq_enable = 0x0; // disable alarm irq before setting
        // 清空 irq 状态
        regs->clear_interrupt = 0x1; // write any value to clear interrupt
        // 启用irq
        regs->irq_enable = 0x1; // enable alarm irq

        // 设置 alarm 时间
        // 设置高位
        regs->alarm_high = nanos_hi;
        // 设置低位
        regs->alarm_low = nanos_lo;
        auto enable_res = _virqs[0]->enable();

        // read alarm status
        auto alarm_status = regs->alarm_status;
        loggers::SUSTCORE::INFO("alarm_status: 0x%08x", alarm_status);
        assert (enable_res.has_value());
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

        // 以第一个 virq 作为 RTC virq
        if (virqs.empty())
        {
            loggers::DEVICE::ERROR("Goldfish RTC 设备缺失 VIRQ 资源!");
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        virq_t virq = virqs[0]->virq();

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
            DriverBase::DevRes(node, std::move(virqs), std::move(mmios)),
            base, virq);
    }
}  // namespace driver
