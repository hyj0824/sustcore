/**
 * @file setup.c
 * @author theflysong (song_of_the_fly@163.com)
 * @brief RISCV64启动程序
 * @version alpha-1.0.0
 * @date 2025-11-21
 *
 * @copyright Copyright (c) 2025
 *
 */

#include <arch/riscv64/csr.h>
#include <arch/riscv64/device/fdt_helper.h>
#include <arch/riscv64/device/misc.h>
#include <arch/riscv64/trait.h>
#include <device/int.h>
#include <logger.h>
#include <libfdt.h>
#include <sbi/sbi.h>
#include <sus/logger.h>
#include <sus/units.h>

#include <cstddef>
#include <cstring>

size_t hart_id;
void *dtb_ptr;

namespace
{
    constexpr size_t BOOT_DTB_STORAGE_SIZE = 128 * 1024;
    alignas(PAGESIZE) unsigned char boot_dtb_storage[BOOT_DTB_STORAGE_SIZE];

    PhyAddr boot_dtb_storage_pa(void) {
        addr_t storage_addr = reinterpret_cast<addr_t>(boot_dtb_storage);
        if (within_scope(storage_addr, AddrType::KVA)) {
            return PhyAddr(KVA2PA(storage_addr));
        }
        if (within_scope(storage_addr, AddrType::KPA)) {
            return PhyAddr(KPA2PA(storage_addr));
        }
        return PhyAddr(storage_addr);
    }

    void *boot_dtb_storage_preinit_addr(void) {
        return boot_dtb_storage_pa().addr();
    }

    [[noreturn]]
    void halt_on_fdt_error(const char *message) {
        loggers::DEVICE::FATAL("%s", message);
        while (true);
    }
}  // namespace

void Riscv64Serial::serial_write_char(char ch) {
    sbi_dbcn_console_write_byte(ch);
}

void Riscv64Serial::serial_write_string(size_t len, const char *str) {
    sbi_dbcn_console_write(len, str);
}

extern "C" void c_setup(void) {
    kernel_setup();
    while (true);
}

void Riscv64Initialization::pre_init(void) {
    if (FDTHelper::fdt_init(dtb_ptr) == nullptr) {
        halt_on_fdt_error("原始FDT初始化失败");
    }

    int dtb_size = fdt_totalsize(dtb_ptr);
    if (dtb_size <= 0) {
        halt_on_fdt_error("原始FDT大小非法");
    }
    if (static_cast<size_t>(dtb_size) > BOOT_DTB_STORAGE_SIZE) {
        halt_on_fdt_error("原始FDT超过内核静态保留区容量");
    }

    void *static_dtb = boot_dtb_storage_preinit_addr();
    memmove(static_dtb, dtb_ptr, static_cast<size_t>(dtb_size));
    dtb_ptr = static_dtb;

    if (FDTHelper::fdt_init(dtb_ptr) == nullptr) {
        halt_on_fdt_error("静态FDT副本初始化失败");
    }
}

void Riscv64Initialization::promote_dtb_to_kpa(void) {
    KpaAddr static_dtb = convert<KpaAddr>(boot_dtb_storage_pa());
    dtb_ptr            = static_dtb.addr();
    if (FDTHelper::fdt_init(dtb_ptr) == nullptr) {
        halt_on_fdt_error("KPA高地址FDT副本初始化失败");
    }
}

void Riscv64Idle::idle()
{
    while(true);
}

void Riscv64Initialization::post_init(void) {
    units::frequency freq = get_clock_freq();
    if (freq.to_mhz() == 0) {
        // 使用QEMU virt机器的默认值10MHz
        freq = 10_MHz;
        loggers::DEVICE::ERROR("获取时钟频率失败, 使用默认值 %d Hz", freq);
    }
    loggers::DEVICE::INFO("时钟频率: %d Hz = %d KHz = %d MHz", freq.to_hz(), freq.to_khz(), freq.to_mhz());
}
