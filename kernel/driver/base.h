/**
 * @file base.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 驱动基类
 * @version alpha-1.0.0
 * @date 2026-05-31
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <device/device.h>
#include <device/resource.h>

namespace driver {
    /**
     * @brief 驱动对象基本类
     *
     * 该类不拥有 DeviceNode, 仅通过统一语义接口访问底层设备属性.
     */
    class DriverBase {
    public:
        /**
         * @brief 驱动对象基本类
         *
         * @param node 设备节点非拥有指针.
         */
        explicit DriverBase(const device::DeviceNode &node) noexcept;

        /**
         * @brief 销毁驱动对象基本类
         */
        virtual ~DriverBase() noexcept;

        /**
         * @brief 获取底层设备节点.
         *
         * @return device::DeviceNode& 对应设备节点引用.
         */
        [[nodiscard]]
        const device::DeviceNode &node() const noexcept {
            return *_node;
        }

        /**
         * @brief 获取设备平台名称.
         *
         * @return const char* 平台名称字符串.
         */
        [[nodiscard]]
        const char *platform() const noexcept {
            return _node != nullptr ? _node->platform() : "unknown";
        }

        /**
         * @brief 获取驱动兼容串.
         *
         * @return std::string_view 驱动兼容串.
         */
        [[nodiscard]]
        virtual std::string_view compatible() const = 0;

        /**
         * @brief 获取驱动名称
         *
         * @return const char* 驱动名称
         */
        [[nodiscard]]
        virtual const char *name() const noexcept {
            return _node != nullptr ? _node->name() : "unknown";
        }

    protected:
        const device::DeviceNode *_node = nullptr;
        std::vector<util::owner<device::VIrqResource *>> _virqs;
        std::vector<util::owner<device::MMIOResource *>> _mmios;

        /**
         * @brief 获取当前驱动持有的 virq 资源列表.
         *
         * @return const std::vector<util::owner<device::VIrqResource *>>& virq
         * 资源列表.
         */
        [[nodiscard]]
        const std::vector<util::owner<device::VIrqResource *>> &virq_resources() const
            noexcept {
            return _virqs;
        }

        /**
         * @brief 获取当前驱动持有的 MMIO 资源列表.
         *
         * @return const std::vector<util::owner<device::MMIOResource *>>&
         * MMIO 资源列表.
         */
        [[nodiscard]]
        const std::vector<util::owner<device::MMIOResource *>> &mmio_resources() const
            noexcept {
            return _mmios;
        }

        /**
         * @brief 加载整数
         *
         * @param node 设备节点
         * @param prop_name 属性名称
         * @param integral_sz 整数大小
         * @return Result<sus_u64> 属性值
         */
        static Result<sus_u64> __load_integral(const device::DeviceNode &node,
                                               std::string_view prop_name,
                                               size_t integral_sz) noexcept;
    };
}  // namespace driver
