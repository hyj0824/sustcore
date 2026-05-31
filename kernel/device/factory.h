/**
 * @file factory.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 设备工厂兼容转发头
 * @version alpha-1.0.0
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <driver/factory.h>

namespace device {
    using driver::IDeviceFactory;
    using driver::IIrqChipFactory;
    using driver::DeviceFactoryRegistry;
    using driver::IrqChipFactoryRegistry;
}  // namespace device
