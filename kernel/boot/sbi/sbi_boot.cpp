/**
 * @file sbi_boot.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief SBI 启动代码
 * @version alpha-1.0.0
 * @date 2026-06-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <boot/sbi/sbi_paging.h>
#include <sustcore/addr.h>

#include <cstddef>

namespace sbi {
    _SBI_PAGING pte_t L0PAGING[PAGE_ENTRIES]
        __attribute__((aligned(PAGE_SIZE)));
    _SBI_PAGING pte_t L1_KERNEL_PAGING[PAGE_ENTRIES]
        __attribute__((aligned(PAGE_SIZE)));
    _SBI_PAGING pte_t L1_IDENTITY_PAGING[PAGE_ENTRIES]
        __attribute__((aligned(PAGE_SIZE)));
}  // namespace sbi

#define _SBI_FUNCTION   SECTION(".sbi_boot.text")
#define _SBI_DATA       SECTION(".sbi_boot.data")
#define _SBI_RODATA     SECTION(".sbi_boot.rodata")
#define _SBI_STRING(x)  _SBI_RODATA constexpr const char x[]
#define _SBI_STRLEN(x)  (sizeof(x) - 1)
#define _SBI_WRITE(str) _sbi_write(_SBI_STRLEN(str), str)

// messages
namespace sbi {
    _SBI_STRING(SBI_BOOT_MSG) = "SBI引导程序启动!\n";
    _SBI_STRING(SBI_PANIC_MSG) = "SBI引导程序发生错误，无法继续执行!\n";
    _SBI_STRING(SBI_INVALID_KERNEL_SIZE_MSG) =
        "错误: 内核大小超过 32MB  限制\n";
    _SBI_STRING(SBI_INVALID_PAGING_SIZE_MSG) = "错误: 分页部分小于 32KB 限制\n";
    _SBI_STRING(SBI_MISALIGNED_PAGING_MSG) = "错误: 分页部分地址未对齐到4KB\n";
    _SBI_STRING(SBI_BOUNDARY_MISALIGNED_MSG) =
        "错误: 内核末尾地址未对齐到2MB\n";
    _SBI_STRING(SBI_CROSS_L1_DIRECTORY_MSG) =
        "错误: 内核跨越了L1页目录边界, 无法使用单级页表映射\n";
    _SBI_STRING(SBI_IMPOSSIBLE)     = "错误: 这不应该发生\n";
    _SBI_STRING(SBI_CHECK_PASS_MSG) = "SBI引导程序检查通过!\n";
    _SBI_STRING(SBI_PAGING_SETUP_COMPLETE_MSG) = "SBI分页设置完成!\n";
    _SBI_STRING(SBI_INVALID_DTB_MAGIC_MSG)     = "错误: DTB魔数不正确\n";
    _SBI_STRING(SBI_INVALID_DTB_SIZE_MSG) = "错误: DTB大小超过限制\n";
}  // namespace sbi

// 通过命名空间避免名称冲突
// 同时严格保证内核程序不会被外部调用
namespace sbi {
    extern "C" size_t __sbi_boot_hart_id;
    extern "C" addr_t __sbi_dtb_phys;
    static_assert(sizeof(__sbi_boot_hart_id) == 8, "类型大小不匹配");
    static_assert(sizeof(__sbi_dtb_phys) == 8, "类型大小不匹配");

    extern "C" _SBI_FUNCTION void _sbi_write(size_t len, const char *buf);
    _SBI_FUNCTION void _sbi_panic() {
        _SBI_WRITE(SBI_PANIC_MSG);
        while (true);
    }

#define _SBI_PANIC(x)  \
    do {               \
        _SBI_WRITE(x); \
        _sbi_panic();  \
    } while (0)

    static _SBI_DATA size_t kva_vpn2;  // 内核虚拟地址的VPN2部分

    // 使用一个 L0 页目录 + 两个 L1 页目录来映射内核空间
    // 总共可覆盖 1GB 内核空间
    _SBI_FUNCTION void mappings(addr_t pa_s, addr_t pa_e) {
        if (pa_s & PAGING_ALIGNMENT_MASK) {
            _SBI_PANIC(SBI_MISALIGNED_PAGING_MSG);
        }

        if (pa_e & PAGING_ALIGNMENT_MASK) {
            _SBI_PANIC(SBI_BOUNDARY_MISALIGNED_MSG);
        }

        // 将 pa_s / pa_e 转换为虚拟地址，并计算其 VPN
        addr_t va_s = pa_s + KVA_OFFSET;
        addr_t va_e = pa_e + KVA_OFFSET;
        size_t vvpns[3], vvpne[3], pvpns[3], pvpne[3];
        TOVPN(vvpns, va_s);
        TOVPN(vvpne, va_e);
        TOVPN(pvpns, pa_s);
        TOVPN(pvpne, pa_e);

        // 做出以下假设:
        // 1. vvpns[2] = vvpne[2]，即其在内核虚拟空间中不跨越L1页目录边界
        if (vvpns[2] != vvpne[2]) {
            _SBI_PANIC(SBI_CROSS_L1_DIRECTORY_MSG);
        }
        kva_vpn2 = vvpns[2];
        // 以下这件事一定成立: pvpns[2] =
        // pvpne[2]，即物理地址不跨越L1页目录边界
        if (pvpns[2] != pvpne[2]) {
            _SBI_PANIC(SBI_IMPOSSIBLE);
        }
        // 设置 L0 页目录项，指向两个 L1 页目录
        L0PAGING[vvpns[2]] = MAKE_PDE(L1_KERNEL_PAGING);
        L0PAGING[pvpns[2]] = MAKE_PDE(L1_IDENTITY_PAGING);

        // 以下这件事一定成立: vvpne[1] - vvpns[1] = pvpne[1] - pvpns[1]
        size_t steps = vvpne[1] - vvpns[1];
        if (steps != (pvpne[1] - pvpns[1])) {
            _SBI_PANIC(SBI_IMPOSSIBLE);
        }

        // 设置 L1 页目录项，设置内核空间虚拟地址与物理地址的映射
        for (size_t i = 0; i <= steps; i++) {
            addr_t pa                        = pa_s + i * PAGE_SIZE_2M;
            L1_KERNEL_PAGING[vvpns[1] + i]   = MAKE_PTE(pa);
            L1_IDENTITY_PAGING[pvpns[1] + i] = MAKE_PTE(pa);
        }
    }

    _SBI_FUNCTION void mapping_dtb(addr_t pa_s, addr_t pa_e) {
        if (pa_s & PAGING_ALIGNMENT_MASK) {
            _SBI_PANIC(SBI_MISALIGNED_PAGING_MSG);
        }

        if (pa_e & PAGING_ALIGNMENT_MASK) {
            _SBI_PANIC(SBI_BOUNDARY_MISALIGNED_MSG);
        }

        addr_t va_s = pa_s + KVA_OFFSET;
        addr_t va_e = pa_e + KVA_OFFSET;
        size_t vvpns[3], vvpne[3];
        TOVPN(vvpns, va_s);
        TOVPN(vvpne, va_e);

        if (vvpns[2] != vvpne[2]) {
            _SBI_PANIC(SBI_CROSS_L1_DIRECTORY_MSG);
        }

        if (vvpns[2] != kva_vpn2) {
            _SBI_PANIC(SBI_CROSS_L1_DIRECTORY_MSG);
        }

        size_t steps = vvpne[1] - vvpns[1];

        // 设置 L1 页目录项，设置内核空间虚拟地址与物理地址的映射
        for (size_t i = 0; i <= steps; i++) {
            addr_t pa                        = pa_s + i * PAGE_SIZE_2M;
            L1_KERNEL_PAGING[vvpns[1] + i]   = MAKE_PTE(pa);
        }
    }

    _SBI_FUNCTION size_t calc_satp() {
        return SATP_SV39_BASE | TO_PPN(L0PAGING);
    }

    _SBI_FUNCTION qword dtb_byte(size_t offset) {
        return *(byte *)(__sbi_dtb_phys + offset);
    }

#define DTB_BYTE(x)  dtb_byte(x)
#define DTB_WORD(x)  (DTB_BYTE(x) << 8 | DTB_BYTE((x) + 1))
#define DTB_DWORD(x) (DTB_WORD(x) << 16 | DTB_WORD((x) + 2))
#define DTB_QWORD(x) (DTB_DWORD(x) << 32 | DTB_DWORD((x) + 4))

    // 返回值: satp 寄存器的值
    extern "C" _SBI_FUNCTION size_t _sbi_setup() {
        _SBI_WRITE(SBI_BOOT_MSG);

        char *paging_start = &s_sbi_paging;
        char *paging_end   = &e_sbi_paging;

        size_t paging_size = paging_end - paging_start;
        if (paging_size < MINIMUM_PAGING_SIZE) {
            _SBI_PANIC(SBI_INVALID_PAGING_SIZE_MSG);
        }

        char *kernel_start = &s_sbi;
        char *kernel_end   = &ekernel_phys;
        size_t kernel_size = kernel_end - kernel_start;

        if (kernel_size > MAXIMUM_KERNEL_SIZE) {
            _SBI_PANIC(SBI_INVALID_KERNEL_SIZE_MSG);
        }

        auto kernel_end_arith = reinterpret_cast<addr_t>(kernel_end);
        if (kernel_end_arith % PAGING_ALIGNMENT != 0) {
            _SBI_PANIC(SBI_BOUNDARY_MISALIGNED_MSG);
        }

        auto kernel_start_arith   = reinterpret_cast<addr_t>(kernel_start);
        auto aligned_kernel_start = kernel_start_arith & ~PAGING_ALIGNMENT_MASK;
        _SBI_WRITE(SBI_CHECK_PASS_MSG);

        dword magic = DTB_DWORD(0);
        if (magic != 0xD00DFEED) {
            _SBI_PANIC(SBI_INVALID_DTB_MAGIC_MSG);
        }
        dword dtb_size = DTB_DWORD(4);
        if (dtb_size > MAXIMUM_DTB_SIZE) {
            _SBI_PANIC(SBI_INVALID_DTB_SIZE_MSG);
        }

        mappings(aligned_kernel_start, kernel_end_arith);
        _SBI_WRITE(SBI_PAGING_SETUP_COMPLETE_MSG);

        auto aligned_dtb_start = __sbi_dtb_phys & ~PAGING_ALIGNMENT_MASK;
        auto dtb_end = __sbi_dtb_phys + dtb_size;
        auto aligned_dtb_end = (dtb_end + PAGING_ALIGNMENT_MASK) & ~PAGING_ALIGNMENT_MASK;
        mapping_dtb(aligned_dtb_start, aligned_dtb_end);

        return calc_satp();
    }
}  // namespace sbi