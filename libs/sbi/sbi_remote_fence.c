/**
 * @file sbi_remote_fence.c
 * @author hyj0824 (2417179808@qq.com)
 * @brief 
 * @version alpha-1.0.0
 * @date 2025-11-21
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <sbi/sbi.h>

SBIRet sbi_remote_fence_i(umb_t hart_mask, umb_t hart_mask_base) {
    return sbi_ecall(SBI_EID_RFNC, SBI_REMOTE_FENCE_I,
                     hart_mask,
                     hart_mask_base,
                     0, 0, 0, 0);
}

SBIRet sbi_remote_sfence_vma(umb_t hart_mask, umb_t hart_mask_base,
                             umb_t start_addr, umb_t size) {
    return sbi_ecall(SBI_EID_RFNC, SBI_REMOTE_SFENCE_VMA,
                     hart_mask,
                     hart_mask_base,
                     start_addr,
                     size,
                     0, 0);
}

SBIRet sbi_remote_sfence_vma_asid(umb_t hart_mask, umb_t hart_mask_base,
                                  umb_t start_addr, umb_t size, umb_t asid) {
    return sbi_ecall(SBI_EID_RFNC, SBI_REMOTE_SFENCE_VMA_ASID,
                     hart_mask,
                     hart_mask_base,
                     start_addr,
                     size,
                     asid, 0);
}

SBIRet sbi_remote_hfence_gvma_vmid(umb_t hart_mask, umb_t hart_mask_base,
                                   umb_t start_addr, umb_t size, umb_t vmid) {
    return sbi_ecall(SBI_EID_RFNC, SBI_REMOTE_HFENCE_GVMA_VMID,
                     hart_mask,
                     hart_mask_base,
                     start_addr,
                     size,
                     vmid, 0);
}

SBIRet sbi_remote_hfence_gvma(umb_t hart_mask, umb_t hart_mask_base,
                              umb_t start_addr, umb_t size) {
    return sbi_ecall(SBI_EID_RFNC, SBI_REMOTE_HFENCE_GVMA,
                     hart_mask,
                     hart_mask_base,
                     start_addr,
                     size,
                     0, 0);
}

SBIRet sbi_remote_hfence_vvma_asid(umb_t hart_mask, umb_t hart_mask_base,
                                   umb_t start_addr, umb_t size, umb_t asid) {
    return sbi_ecall(SBI_EID_RFNC, SBI_REMOTE_HFENCE_VVMA_ASID,
                     hart_mask,
                     hart_mask_base,
                     start_addr,
                     size,
                     asid, 0);
}

SBIRet sbi_remote_hfence_vvma(umb_t hart_mask, umb_t hart_mask_base,
                              umb_t start_addr, umb_t size) {
    return sbi_ecall(SBI_EID_RFNC, SBI_REMOTE_HFENCE_VVMA,
                     hart_mask,
                     hart_mask_base,
                     start_addr,
                     size,
                     0, 0);
}