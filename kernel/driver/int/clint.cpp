/**
 * @file clint.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief CLINT 设备与驱动
 * @version alpha-1.0.0
 * @date 2026-05-29
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <driver/int/clint.h>
#include <logger.h>

namespace driver {
    /**
     * @brief 创建一个 CLINT 设备驱动.
     */
    Result<util::owner<Clint *>> Clint::create(
        device::DeviceNode *node, intc_t identifier, device::cpuid_t hart_id,
        std::vector<device::cpuid_t> target_harts) noexcept {
        if (node == nullptr) {
            loggers::INTERRUPT::ERROR("Clint 创建失败: node 为空");
            unexpect_return(ErrCode::NULLPTR);
        }
        auto *device =
            new Clint(*node, identifier, hart_id, std::move(target_harts));
        if (device == nullptr) {
            loggers::INTERRUPT::ERROR("Clint 创建失败: 内存不足");
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        if (device->mmio_resources().empty()) {
            loggers::INTERRUPT::ERROR("Clint[%u] 创建失败: 缺少 MMIO 资源",
                                      identifier);
            delete device;
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        if (device->virq_resources().size() < 2) {
            loggers::INTERRUPT::ERROR(
                "Clint[%u] 创建失败: virq 资源数量不足 count=%u",
                identifier,
                static_cast<unsigned>(device->virq_resources().size()));
            delete device;
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        loggers::INTERRUPT::DEBUG(
            "创建 Clint 设备: id=%u hart=%u target_harts=%u sw_virq=%llu clock_virq=%llu",
            identifier, hart_id,
            static_cast<unsigned>(device->_target_harts.size()),
            static_cast<unsigned long long>(device->software_virq()),
            static_cast<unsigned long long>(device->clock_virq()));
        return util::owner<Clint *>(device);
    }

    /**
     * @brief 构造一个 CLINT 设备驱动.
     */
    Clint::Clint(const device::DeviceNode &node, intc_t identifier,
                 device::cpuid_t hart_id,
                 std::vector<device::cpuid_t> target_harts) noexcept
        : device::IrqChip(node),
          _identifier(identifier),
          _hart_id(hart_id),
          _target_harts(std::move(target_harts)) {}

    std::string_view Clint::compatible() const noexcept {
        return "riscv,clint0";
    }

    /**
     * @brief 获取 MMIO 区域列表.
     */
    std::vector<PhyArea> Clint::mmio_regions() const noexcept {
        std::vector<PhyArea> regions;
        regions.reserve(mmio_resources().size());
        for (const auto &resource : mmio_resources()) {
            assert(resource != nullptr);
            regions.push_back(resource->region());
        }
        return regions;
    }

    /**
     * @brief 获取设备标识.
     */
    intc_t Clint::identifier() const noexcept {
        return _identifier;
    }

    /**
     * @brief 获取默认 hart.
     */
    device::cpuid_t Clint::hart_id() const noexcept {
        return _hart_id;
    }

    /**
     * @brief 获取目标 hart 集合.
     */
    const std::vector<device::cpuid_t> &Clint::target_harts() const noexcept {
        return _target_harts;
    }

    /**
     * @brief 判断指定 hart 是否受该 CLINT 覆盖.
     */
    bool Clint::supports_hart(device::cpuid_t hart_id) const noexcept {
        for (auto target : _target_harts) {
            if (target == hart_id) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief 获取 software interrupt 的 virq.
     */
    virq_t Clint::software_virq() const noexcept {
        assert(virq_resources().size() >= 2);
        assert(virq_resources().at(0) != nullptr);
        return virq_resources().at(0)->virq();
    }

    /**
     * @brief 获取 clock interrupt 的 virq.
     */
    virq_t Clint::clock_virq() const noexcept {
        assert(virq_resources().size() >= 2);
        assert(virq_resources().at(1) != nullptr);
        return virq_resources().at(1)->virq();
    }

    /**
     * @brief 使能本地中断.
     */
    Result<void> Clint::enable_irq(hwirq_t hw_irq) noexcept {
        loggers::INTERRUPT::ERROR("Clint[%u] 不支持 enable hwirq=%u",
                                  identifier(), hw_irq);
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 屏蔽本地中断.
     */
    Result<void> Clint::disable_irq(hwirq_t hw_irq) noexcept {
        loggers::INTERRUPT::ERROR("Clint[%u] 不支持 disable hwirq=%u",
                                  identifier(), hw_irq);
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 设置中断优先级.
     */
    Result<void> Clint::set_priority(hwirq_t hw_irq,
                                     domain_t prio) noexcept {
        loggers::INTERRUPT::DEBUG(
            "Clint[%u] set_priority hwirq=%u prio=%u 不支持",
            identifier(), hw_irq, prio);
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 设置中断亲和性.
     */
    Result<void> Clint::set_affinity(hwirq_t hw_irq,
                                     cpu_mask_t mask) noexcept {
        loggers::INTERRUPT::ERROR(
            "Clint[%u] set_affinity hwirq=%u mask=0x%llx 不支持",
            identifier(), hw_irq,
            static_cast<unsigned long long>(mask));
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 应答中断.
     */
    Result<void> Clint::ack_irq(hwirq_t hw_irq) noexcept {
        loggers::INTERRUPT::DEBUG("Clint[%u] ack_irq hwirq=%u 不支持",
                                  identifier(), hw_irq);
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    /**
     * @brief 设置中断触发方式.
     */
    Result<void> Clint::set_trigger(hwirq_t hw_irq,
                                    device::IrqTrigger trigger) noexcept {
        loggers::INTERRUPT::DEBUG(
            "Clint[%u] set_trigger hwirq=%u trigger=%d 不支持",
            identifier(), hw_irq, static_cast<int>(trigger));
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }
}  // namespace driver
