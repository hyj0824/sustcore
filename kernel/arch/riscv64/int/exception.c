/**
 * @file exception.c
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 异常处理程序
 * @version alpha-1.0.0
 * @date 2025-11-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include <arch/riscv64/int/exception.h>
#include <arch/riscv64/int/trap.h>

#include <basec/logger.h>

void general_isr(void);
// void general_exception(void);

__attribute__((section(".text")))
dword IVT[IVT_ENTRIES] = {};

static dword emit_j_ins(const dword offset) {
    if (offset & 0x3) {
        // 偏移量必须是4字节对齐的
        return 0;
    }

    const dword j_opcode = 0x6F;
    const dword imm20    = (offset >> 20) & 0x1;
    const dword imm10_1  = (offset >> 1 ) & 0x3FF;
    const dword imm11    = (offset >> 11) & 0x1;
    const dword imm19_12 = (offset >> 12) & 0xFF;

    const dword imm =
        (imm20    << 31 )|
        (imm10_1  << 21 )|
        (imm11    << 20 )|
        (imm19_12 << 12 );

    return imm | j_opcode;
}

static dword emit_ivt_entry(ISRService isr_func, int idx) {
    qword q_off = (qword)isr_func - (qword)IVT - (idx * sizeof(dword));
    return emit_j_ins((dword)q_off);
}

void init_ivt() {
    // 设置为vectorized模式
    // 出于某种未知的原因, vectorized模式似乎卡住了
    // 浪费了我1h用于调试......
    // umb_t stvec = (umb_t)IVT;
    // stvec |= 0x01;

    // for (int i = 0 ; i < IVT_ENTRIES; i++) {
    //     IVT[i] = emit_ivt_entry(general_isr, i);
    // }

    // log_info("general_isr 地址: 0x%lx", (umb_t)general_isr);
    // log_info("general_exception 地址: 0x%lx", (umb_t)general_exception);
    // log_info("IVT 地址: 0x%lx", (umb_t)IVT);


    // 还是采用direct模式
    umb_t stvec = (umb_t)general_isr;
    if (stvec & 0x3) {
        log_info("错误: stvec地址未对齐!");
        return;
    }

    asm volatile("csrw stvec, %0" : : "r"(stvec));
}

ISR_SERVICE_ATTRIBUTE
void general_isr(void) {
    ISR_SERVICE_START(general_isr, 128);
    log_info("中断处理程序被调用!");
    log_info("scause: 0x%lx, sepc: 0x%lx, stval: 0x%lx", scause, sepc, stval);
    log_info("reglist_ptr: 0x%lx", reglist_ptr);

    for (int i = 0; i < 31; i++) {
        log_info("x%d: 0x%lx", i + 1, reglist_ptr->regs[i]);
    }
    log_info("sepc: 0x%lx", reglist_ptr->sepc);
    log_info("sstatus: 0x%lx", reglist_ptr->sstatus);

    if ( (reglist_ptr->sstatus >> 8) & 0x1 ) {
        log_info("中断发生在S-Mode");
    } else {
        log_info("中断发生在U-Mode");
    }

    //TODO: 根据中断类型进行处理
    reglist_ptr->sepc += 4;  // 跳过导致中断的指令

    ISR_SERVICE_END(general_isr);
}

// ISR_SERVICE_ATTRIBUTE
// void general_exception(void) {
//     ISR_SERVICE_START(general_exception, 128);
//     log_info("异常处理程序被调用!");
//     log_info("scause: 0x%lx, sepc: 0x%lx, stval: 0x%lx", scause, sepc, stval);
//     log_info("reglist_ptr: 0x%lx", reglist_ptr);

//     for (int i = 0; i < 31; i++) {
//         log_info("x%d: 0x%lx", i + 1, reglist_ptr->regs[i]);
//     }
//     log_info("sepc: 0x%lx", reglist_ptr->sepc);
//     log_info("sstatus: 0x%lx", reglist_ptr->sstatus);

//     if ( (reglist_ptr->sstatus >> 8) & 0x1 ) {
//         log_info("异常发生在S-Mode");
//     } else {
//         log_info("异常发生在U-Mode");
//     }
//     while (true);
//     ISR_SERVICE_END(general_exception);
// }