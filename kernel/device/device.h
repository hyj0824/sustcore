/**
 * @file device.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 设备抽象
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sus/types.h>
#include <sustcore/addr.h>
#include <sustcore/errcode.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace driver {
    using domain_t                              = b32;
    using irq_prio_t                            = b32;
    using cpu_mask_t                            = b64;
    using virq_t                                = b64;
    using hwirq_t                               = b32;
    using intc_t                                = b32;
    inline constexpr intc_t INVALID_ICTRL_ID    = 0xFFFFFFFF;
    inline constexpr domain_t INVALID_DOMAIN_ID = 0xFFFFFFFF;
    inline constexpr virq_t INVALID_VIRQ        = 0xFFFFFFFF'FFFFFFFFuLL;

    struct IrqEvent;
    using IrqHandler = std::function<void(const IrqEvent &)>;
}  // namespace driver

namespace device {
    // types and constants
    using driver::cpu_mask_t;
    using driver::domain_t;
    using driver::hwirq_t;
    using driver::intc_t;
    using driver::irq_prio_t;
    using driver::virq_t;
    inline constexpr intc_t INVALID_ICTRL_ID    = driver::INVALID_ICTRL_ID;
    inline constexpr domain_t INVALID_DOMAIN_ID = driver::INVALID_DOMAIN_ID;
    using cpuid_t                               = b32;
    using topo_t                                = b32;

    constexpr const char *STANDARD_COMPATIBLE_KEY = "compatible";
    constexpr const char *STANDARD_MMIO_KEY = "mmio";  // "regs" key in DTB
    // "interrupts" + "interrupt-parent" / "interrupts-extended" keys in DTB
    constexpr const char *STANDARD_IRQ_KEY  = "irqs";
    constexpr const char *STANDARD_INTERRUPT_PARENT_KEY = "interrupt-parent";

    /**
     * @brief 统一设备属性值的轻量视图.
     *
     * 对于可直接引用平台原始缓冲区的属性, 该对象仅保存原始数据指针和长度.
     * 对于 `REGION_LIST` 与 `VIRQ_LIST`, 结构化结果会保存在对象内部.
     */
    class DevicePropView {
    public:
        /**
         * @brief 统一设备属性类型枚举.
         */
        enum class PropType {
            // 无值(标记型属性)
            NONE,
            // 字节数组
            // 总是可以被解析为 BYTE_ARRAY
            BYTE_ARRAY,
            // 字符串
            // 只有 STRING 可以被解析为 STRING
            STRING,
            // 字符串列表
            // 只有 STRING 和 STRING_LIST 可以被解析为 STRING_LIST
            STRING_LIST,
            // 整数
            // 只有 INTEGER 可以被解析为 INTEGER
            INTEGER,
            // 整数列表
            // 只有 INTEGER 和 INTEGER_LIST 可以被解析为 INTEGER_LIST
            INTEGER_LIST,
            // 以上五种的任意一种, 一般用于 fdt 后端不识别的属性
            ANY,
            // 类型关系如下图所示:

            //          ANY               |
            //    /              \        |
            //    |              |        |
            //  STRING          INTEGER   |
            //    |              |        |
            // STRING_LIST INTEGER_LIST   |
            //    |             |         |
            //    \             /         |
            //      BYTE_ARRAY            |

            // 下面两种类型不能被 ANY 替代,
            // 只有后端指定的属性能以以下两种方式解析
            REGION_LIST,  // 内存区域列表 (address + size)
            VIRQ_LIST     // 虚拟中断列表 (virq)
        };

        /**
         * @brief 构造一个空属性视图.
         */
        DevicePropView() noexcept = default;

        /**
         * @brief 构造一个直接引用原始数据的属性视图.
         *
         * @param type 属性类型.
         * @param data 原始数据首地址.
         * @param size 原始数据字节数.
         */
        DevicePropView(PropType type, const byte *data, size_t size) noexcept;

        /**
         * @brief 使用内部缓存的 region 列表构造属性视图.
         *
         * @param regions 结构化 MMIO 区域列表.
         * @return DevicePropView 结果视图.
         */
        [[nodiscard]]
        static DevicePropView from_region_list(
            std::vector<PhyArea> regions) noexcept;

        /**
         * @brief 使用内部缓存的 virq 列表构造属性视图.
         *
         * @param virqs 结构化 virq 列表.
         * @return DevicePropView 结果视图.
         */
        [[nodiscard]]
        static DevicePropView from_virq_list(
            std::function<std::vector<driver::virq_t>()> loader) noexcept;

        ~DevicePropView() = default;

        [[nodiscard]]
        PropType type() const noexcept;
        [[nodiscard]]
        std::vector<byte> raw_bytes() const;
        [[nodiscard]]
        std::vector<byte> as_byte_array() const;
        [[nodiscard]]
        std::string_view as_string() const;
        [[nodiscard]]
        std::vector<std::string_view> as_string_list() const;
        [[nodiscard]]
        sus_u64 as_integer(size_t cellsz) const;
        [[nodiscard]]
        std::vector<sus_u64> as_integer_list(size_t cellsz) const;
        [[nodiscard]]
        std::vector<PhyArea> as_region_list() const;
        [[nodiscard]]
        std::vector<driver::virq_t> as_virq_list() const;

    private:
        /**
         * @brief 按大端序解析定长无符号整数.
         *
         * @param data 原始缓冲区起始地址.
         * @param size 字段长度.
         * @return uint64_t 解析结果.
         */
        [[nodiscard]]
        static uint64_t parse_be_integer(const byte *data,
                                         size_t size) noexcept;

        PropType _type    = PropType::NONE;
        const byte *_data = nullptr;
        size_t _size      = 0;
        std::vector<PhyArea> _regions;
        mutable bool _virq_lazy   = false;
        mutable bool _virq_loaded = false;
        std::function<std::vector<driver::virq_t>()> _virq_loader;
        mutable std::vector<driver::virq_t> _virqs = {};
    };

    /**
     * @brief 统一平台设备节点语义接口.
     *
     * 每个具体设备对象均应依赖该接口, 而非直接访问 DTB/ACPI 等平台原始结构.
     */
    class DeviceNode {
    public:
        virtual ~DeviceNode() = default;

        /**
         * @brief 获取统一设备节点名称.
         *
         * @return const char* 设备名称字符串; FDT 下为节点名.
         */
        [[nodiscard]]
        virtual const char *name() const noexcept = 0;

        /**
         * @brief DeviceNode 所属平台 (riscv64, loongarch64等)
         *
         * @return const char* 平台名称字符串
         */
        [[nodiscard]]
        virtual const char *platform() const noexcept = 0;

        [[nodiscard]]
        virtual Optional<DevicePropView> property(
            const std::string_view &name) const = 0;

        [[nodiscard]]
        std::vector<std::string_view> compatibles() const {
            auto prop_res = property(STANDARD_COMPATIBLE_KEY);
            if (!prop_res.has_value()) {
                return {};
            }
            const auto &compatible_prop = prop_res.value();
            const auto type             = compatible_prop.type();
            assert(type == DevicePropView::PropType::STRING ||
                   type == DevicePropView::PropType::STRING_LIST);
            return compatible_prop.as_string_list();
        }

        /**
         * @brief 该设备是否与给定兼容性字符串匹配.
         *
         * @param compatible 兼容性字符串
         * @return int 与兼容性字符串匹配的结果, -1 表示不匹配 ; 否则,
         * 返回值代表这是第几个兼容项匹配成功
         */
        [[nodiscard]]
        int is_compatible_with(std::string_view compatible) const {
            auto _compatibles = compatibles();
            auto it           = std::ranges::find(_compatibles, compatible);
            if (it == _compatibles.end()) {
                return -1;
            }
            return static_cast<int>(std::distance(_compatibles.begin(), it));
        }

        [[nodiscard]]
        Optional<std::vector<PhyArea>> mmio_regions() const {
            auto prop_res = property(STANDARD_MMIO_KEY);
            if (!prop_res.has_value()) {
                return std::nullopt;
            }
            const auto &mmio_prop = prop_res.value();
            if (mmio_prop.type() != DevicePropView::PropType::REGION_LIST) {
                return std::nullopt;
            }
            return mmio_prop.as_region_list();
        }

        [[nodiscard]]
        Optional<std::vector<driver::virq_t>> irqs() const {
            auto prop_res = property(STANDARD_IRQ_KEY);
            if (!prop_res.has_value()) {
                return std::nullopt;
            }
            const auto &irq_prop = prop_res.value();
            if (irq_prop.type() != DevicePropView::PropType::VIRQ_LIST) {
                return std::nullopt;
            }
            return irq_prop.as_virq_list();
        }

        /**
         * @brief 获取统一语义下的中断父节点标识.
         *
         * @return Optional<sus_u64> 中断父节点标识, 缺失时返回空.
         */
        [[nodiscard]]
        Optional<sus_u64> interrupt_parent() const {
            auto prop_res = property(STANDARD_INTERRUPT_PARENT_KEY);
            if (!prop_res.has_value()) {
                return std::nullopt;
            }
            const auto &parent_prop = prop_res.value();
            if (parent_prop.type() != DevicePropView::PropType::INTEGER) {
                return std::nullopt;
            }
            return parent_prop.as_integer(sizeof(sus_u32));
        }
    };
}  // namespace device
