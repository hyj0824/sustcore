/**
 * @file serial.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 串口设备与工厂实现
 * @version alpha-1.0.0
 * @date 2026-05-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/model.h>
#include <device/resource.h>
#include <driver/serial.h>
#include <logger.h>
#include <sus/units.h>

namespace driver {
    /**
     * @brief 构造一个串口设备驱动.
     */
    SerialDevice::SerialDevice(DevRes res, units::frequency clock_frequency,
                               char *base) noexcept
        : DriverBase(std::move(res)),
          _clock_frequency(clock_frequency),
          _base(reinterpret_cast<volatile UART *>(base)) {
        assert(_mmios.size() >= 1);
        auto *mmio = _mmios[0].get();
        assert(mmio != nullptr);
        assert(mmio->region().size() >= UART_REG_SIZE);
    }

    /**
     * @brief 获取 UART 输入时钟频率.
     */
    units::frequency SerialDevice::clock_frequency() const noexcept {
        return _clock_frequency;
    }

    void SerialDevice::writec(char ch) noexcept {
        _base->thr = static_cast<uart_t>(ch) & 0xFF;
    }

    void SerialDevice::write(const char *str, size_t len) noexcept {
        for (size_t i = 0; i < len; ++i) {
            writec(str[i]);
        }
    }

    /**
     * @brief 获取该工厂支持的主 compatible.
     */
    std::string_view SerialDeviceFactory::compatible() const noexcept {
        return SerialDevice::NS16550A_COMPATIBLE;
    }

    /**
     * @brief 基于统一设备节点创建串口设备驱动.
     */
    Result<DriverBase *> SerialDeviceFactory::create(
        const device::DeviceNode &node, device::DeviceModel &model) const {
        (void)model;
        loggers::DEVICE::INFO("开始创建设备驱动: serial name=%s", node.name());

        auto load_res = SerialDevice::__load_integral(
            node, SerialDevice::CLOCK_FREQUENCY_PROP, sizeof(sus_u32));
        propagate(load_res);
        auto clock_frequency = units::frequency::from_hz(load_res.value());
        if (clock_frequency.to_milihz() == 0) {
            loggers::DEVICE::ERROR("SerialDevice 创建失败: 时钟频率为 0");
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        loggers::DEVICE::DEBUG(
            "SerialDevice[%s] 时钟频率=%llu Hz", node.name(),
            static_cast<unsigned long long>(clock_frequency.to_hz()));

        auto virqs = device::DevResManager::get_virq_resource(node);
        auto mmios = device::DevResManager::get_mmio_resource(node);
        if (mmios.empty()) {
            loggers::DEVICE::ERROR("SerialDevice 创建失败: 缺少 MMIO 资源");
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        auto *mmio = mmios.front().get();
        assert(mmio != nullptr);
        if (mmio->region().size() < SerialDevice::UART_REG_SIZE) {
            loggers::DEVICE::ERROR(
                "SerialDevice 创建失败: MMIO 区域过小 size=%llu need=%llu",
                static_cast<unsigned long long>(mmio->region().size()),
                static_cast<unsigned long long>(SerialDevice::UART_REG_SIZE));
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        auto map_res = device::MMIOManager::inst().map_to_kernel(*mmio);
        if (!map_res.has_value()) {
            loggers::DEVICE::ERROR(
                "SerialDevice 创建失败: MMIO 映射失败 err=%s",
                to_cstring(map_res.error()));
            propagate_return(map_res);
        }
        auto res_pack =
            DriverBase::DevRes(node, std::move(virqs), std::move(mmios));

        auto *device = new SerialDevice(std::move(res_pack), clock_frequency,
                                        map_res.value().as<char>());
        if (device == nullptr) {
            loggers::DEVICE::ERROR("SerialDevice 创建失败: 内存不足");
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        loggers::DEVICE::DEBUG(
            "创建 SerialDevice: name=%s clock=%llu "
            "compatible=%s",
            device->name(),
            static_cast<unsigned long long>(device->_clock_frequency.to_hz()),
            std::string(device->compatible()).c_str());
        return device;
    }
}  // namespace driver
