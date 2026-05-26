/**
 * @file model.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 设备模型 (ACPI/DTB 的抽象表示)
 * @version alpha-1.0.0
 * @date 2026-05-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <device/cpu.h>
#include <device/int.h>
#include <logger.h>
#include <sus/owner.h>
#include <sustcore/addr.h>
#include <sustcore/errcode.h>

#include <vector>

namespace device {
    /**
     * @brief 内存区域
     *
     */
    struct MemRegion {
        /**
         * @brief 内存状态
         *
         */
        enum class MemoryStatus {
            FREE             = 0,
            RESERVED         = 1,
            ACPI_RECLAIMABLE = 2,
            ACPI_NVS         = 3,
            IOMMU            = 4,
            BAD_MEMORY       = 5
        };

        PhyArea area;
        MemoryStatus status;
    };

    class DeviceProvider {
    public:
        virtual ~DeviceProvider() = default;

        /**
         * @brief 收集设备模型中所有的内存区域信息
         *
         * @param regions 输出参数, 用于存储收集到的内存区域列表.
         * 每个区域包含起始地址、大小和状态等信息.
         */
        virtual void collect_memory_regions(
            std::vector<MemRegion> &regions) const = 0;

        virtual void update_cpus(CpuGroupInfo &cpus) const = 0;

        [[nodiscard]]
        virtual const char *name() const = 0;
    };

    class DeviceModel {
    public:
        [[nodiscard]]
        std::vector<MemRegion> memory_regions() const {
            return _regions;
        }

        [[nodiscard]]
        const CpuGroupInfo &cpus() const {
            return _cpus;
        }

        void register_provider(util::owner<DeviceProvider *> provider) {
            _providers.push_back(std::move(provider));
            provider->collect_memory_regions(_regions);
            _regions = _normalize_memory_regions(_regions);
            provider->update_cpus(_cpus);
            loggers::DEVICE::INFO("已注册设备提供者: %s", provider->name());
        }

        DeviceModel(const DeviceModel &)            = delete;
        DeviceModel &operator=(const DeviceModel &) = delete;
        DeviceModel(DeviceModel &&other)            = delete;
        DeviceModel &operator=(DeviceModel &&other) = delete;

        ~DeviceModel() {
            cleanup();
        }
        void cleanup() {
            for (auto &provider : _providers) {
                delete provider.get();
            }
            _providers.clear();
        }

        static DeviceModel &inst();
        static void init();
        static bool initialized();

    protected:
        /**
         * @brief 规范化内存区域列表
         *
         * 该函数接受一个可能包含重叠或未排序的内存区域列表, 并返回一个新的列表,
         * 其会依次执行:
         * 1. 将重叠的同状态区域合并为单个连续区域
         * 2. 从 FREE 状态中剔除任何与非 FREE 区域重叠的部分
         * 3. 如果有两个非 FREE 内存区域重叠, 则将重叠部分设置为 BAD_MEMORY 状态
         * 4. 剔除所有空区域 (大小为 0 的区域, 例如上述结果中的 [0x4000, 0x3000)
         * 就是一个空区域)
         * 5. 将区域按起始地址排序
         *
         * @param regions 待规范化的内存区域列表
         * @return std::vector<MemRegion> 规范化后的内存区域列表
         */
        [[nodiscard]]
        std::vector<MemRegion> _normalize_memory_regions(
            const std::vector<MemRegion> &regions) const;

    private:
        static DeviceModel _INSTANCE;
        static bool _initialized;
        constexpr DeviceModel() = default;
        std::vector<util::owner<DeviceProvider *>> _providers;
        std::vector<MemRegion> _regions;
        CpuGroupInfo _cpus;
    };

    class KernelProvider : public DeviceProvider {
    public:
        void collect_memory_regions(
            std::vector<MemRegion> &regions) const override;
        void update_cpus(CpuGroupInfo &) const override {}
        [[nodiscard]]
        const char *name() const override {
            return "kernel";
        }
    };
};  // namespace device
