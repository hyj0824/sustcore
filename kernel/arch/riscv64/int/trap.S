/**
 * @file trap.S
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 异常处理程序入口, 出口汇编代码
 * @version alpha-1.0.0
 * @date 2025-11-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../hart.S"

STACK_SIZE = CTX_SLOT_OFFSET(CTX_SLOT_COUNT)

.macro ctx_store base, slot, reg
    sd \reg, CTX_SLOT_OFFSET(\slot)(\base)
.endm

.macro ctx_load reg, base, slot
    ld \reg, CTX_SLOT_OFFSET(\slot)(\base)
.endm

.macro save_kernel_switch_context ctx, resume
    ctx_store \ctx, CTX_RA_SLOT, ra
    ctx_store \ctx, CTX_SP_SLOT, sp
    ctx_store \ctx, CTX_S0_SLOT, s0
    ctx_store \ctx, CTX_S1_SLOT, s1
    ctx_store \ctx, CTX_S2_SLOT, s2
    ctx_store \ctx, CTX_S3_SLOT, s3
    ctx_store \ctx, CTX_S4_SLOT, s4
    ctx_store \ctx, CTX_S5_SLOT, s5
    ctx_store \ctx, CTX_S6_SLOT, s6
    ctx_store \ctx, CTX_S7_SLOT, s7
    ctx_store \ctx, CTX_S8_SLOT, s8
    ctx_store \ctx, CTX_S9_SLOT, s9
    ctx_store \ctx, CTX_S10_SLOT, s10
    ctx_store \ctx, CTX_S11_SLOT, s11
    la t0, \resume
    ctx_store \ctx, CTX_SEPC_SLOT, t0
.endm

.macro restore_kernel_switch_context ctx
    ctx_load ra, \ctx, CTX_RA_SLOT
    ctx_load sp, \ctx, CTX_SP_SLOT
    ctx_load s0, \ctx, CTX_S0_SLOT
    ctx_load s1, \ctx, CTX_S1_SLOT
    ctx_load s2, \ctx, CTX_S2_SLOT
    ctx_load s3, \ctx, CTX_S3_SLOT
    ctx_load s4, \ctx, CTX_S4_SLOT
    ctx_load s5, \ctx, CTX_S5_SLOT
    ctx_load s6, \ctx, CTX_S6_SLOT
    ctx_load s7, \ctx, CTX_S7_SLOT
    ctx_load s8, \ctx, CTX_S8_SLOT
    ctx_load s9, \ctx, CTX_S9_SLOT
    ctx_load s10, \ctx, CTX_S10_SLOT
    ctx_load s11, \ctx, CTX_S11_SLOT
    ctx_load t0, \ctx, CTX_SEPC_SLOT
    jr t0
.endm

.section .text
.globl isr_entry
.extern handle_trap
/* ISR入口 */
.balign 4
isr_entry:
save_context:
    /* 第1步: 切换到内核栈空间 */
    /* 按约定, sscratch存放了该线程的内核栈顶地址, 因此直接交换sp和sscratch即可切换到内核栈空间 */
    csrrw sp, sscratch, sp
    /* 但如果sp为零, 说明之前是S-Mode, 否则是U-Mode, 进入不同情况对栈寄存器的处理 */
    bnez sp, isr_ustks
/* S-Mode Stack Save*/
isr_sstks:
    /* 第2步: 从sscratch中恢复sp */
    /* 此时sp = 0, sscratch中保存的就是原sp, 直接交换即可恢复sp */
    csrrw sp, sscratch, sp
    /* 分配栈空间并保存x1, x2 */
    addi sp, sp, -STACK_SIZE
    ctx_store sp, CTX_RA_SLOT, x1
    /* 令 x1 指向原栈顶, 即原x2(sp) */
    addi x1, sp, STACK_SIZE
    /* 保存x2 */
    ctx_store sp, CTX_SP_SLOT, x1
    j isr_sregs
/* U-Mode Stack Save*/
isr_ustks:
    /* 此时, sp 为内核栈顶地址 */
    /* 分配栈空间并保存x1, x2 */
    addi sp, sp, -STACK_SIZE
    ctx_store sp, CTX_RA_SLOT, x1
    /* 令 x1 指向原栈顶, 即原x2(sp) */
    csrr x1, sscratch
    /* 保存 x2 */
    ctx_store sp, CTX_SP_SLOT, x1
    /* 进入内核后 sscratch 固定为 0 */
    csrw sscratch, zero
/* 通用寄存器保存(除了x1, x2) */
isr_sregs:
    ctx_store sp, CTX_GP_SLOT, gp
    ctx_store sp, CTX_T0_SLOT, t0
    ctx_store sp, CTX_T1_SLOT, t1
    ctx_store sp, CTX_T2_SLOT, t2
    ctx_store sp, CTX_S0_SLOT, s0
    ctx_store sp, CTX_S1_SLOT, s1
    ctx_store sp, CTX_A0_SLOT, a0
    ctx_store sp, CTX_A1_SLOT, a1
    ctx_store sp, CTX_A2_SLOT, a2
    ctx_store sp, CTX_A3_SLOT, a3
    ctx_store sp, CTX_A4_SLOT, a4
    ctx_store sp, CTX_A5_SLOT, a5
    ctx_store sp, CTX_A6_SLOT, a6
    ctx_store sp, CTX_A7_SLOT, a7
    ctx_store sp, CTX_S2_SLOT, s2
    ctx_store sp, CTX_S3_SLOT, s3
    ctx_store sp, CTX_S4_SLOT, s4
    ctx_store sp, CTX_S5_SLOT, s5
    ctx_store sp, CTX_S6_SLOT, s6
    ctx_store sp, CTX_S7_SLOT, s7
    ctx_store sp, CTX_S8_SLOT, s8
    ctx_store sp, CTX_S9_SLOT, s9
    ctx_store sp, CTX_S10_SLOT, s10
    ctx_store sp, CTX_S11_SLOT, s11
    ctx_store sp, CTX_T3_SLOT, t3
    ctx_store sp, CTX_T4_SLOT, t4
    ctx_store sp, CTX_T5_SLOT, t5
    ctx_store sp, CTX_T6_SLOT, t6
    /* 在这之后, 通用寄存器就任我们摆布了 */
isr_scsrs:
    # TODO: 我们本应在此处读取hart-id, 并借此更新tp的
    # 但还是等到后面引入 SMP 时再说吧
    # 目前, 我们暂时将hart-id设置为0, 从而得到核心0的Hart Context
    mv t0, zero
    locate_hart_context t0, t1
    /* 保存关键 CSR */
    csrr t0, sepc
    ctx_store sp, CTX_SEPC_SLOT, t0
    csrr t0, sstatus
    ctx_store sp, CTX_SSTATUS_SLOT, t0
    addi t0, sp, STACK_SIZE
    ctx_store sp, CTX_KSTACK_SP_SLOT, t0
    /* 跳转至C异常处理函数 */
    j __handle_trap

__handle_trap:
    /* 传参 */
    csrr a0, scause
    csrr a1, sepc
    csrr a2, stval
    mv   a3, sp
    call handle_trap
    /* 处理完成后, 跳转至ISR出口 */
    j isr_exit

isr_exit:
restore_context:
    /* 理论上, 此时的sp就是我们的内核栈sp了 */
    /* 然而, 此时的sp可能并非sscratch中保存的内核栈顶地址 */
    /* 我们将再次从sscratch中恢复sp. 这样, 内核就可以通过操纵sscratch来控制sp */
    csrrw sp, sscratch, sp
    /* 然而, 若此时sp为0, 则sscratch原先即为0, 即原先并不存在所谓内核栈 */
    /* 因此, 如果sp为0, 我们要从sscratch中恢复sp */
    /* 否则, sp = sscratch - STACK_SIZE */
    bnez sp, isr_ustkr
/* S-Mode Stack Restore */
isr_sstkr:
    /* 此时, sp = 0, sscratch中保存的就是原sp, 直接交换即可恢复sp */
    csrrw sp, sscratch, sp
    /* 恢复通用寄存器 */
    j isr_rcsrs
/* U-Mode Stack Restore */
isr_ustkr:
    /* 此时, sp = 内核栈顶地址 */
    /* 我们需要恢复sscratch, 使其依然指向内核栈顶地址 */
    csrw sscratch, sp
    /* sp 减去STACK_SIZE就是保存寄存器的栈底地址 */
    addi sp, sp, -STACK_SIZE
    /* 用户态才需要恢复tp */
    ctx_load tp, sp, CTX_TP_SLOT
/* CSR恢复 */
isr_rcsrs:
    ctx_load t0, sp, CTX_SSTATUS_SLOT
    csrw sstatus, t0
    ctx_load t0, sp, CTX_SEPC_SLOT
    csrw sepc, t0
/* 通用寄存器恢复(除x2(sp)) */
isr_rregs:
    ctx_load ra, sp, CTX_RA_SLOT
    ctx_load gp, sp, CTX_GP_SLOT
    ctx_load t0, sp, CTX_T0_SLOT
    ctx_load t1, sp, CTX_T1_SLOT
    ctx_load t2, sp, CTX_T2_SLOT
    ctx_load s0, sp, CTX_S0_SLOT
    ctx_load s1, sp, CTX_S1_SLOT
    ctx_load a0, sp, CTX_A0_SLOT
    ctx_load a1, sp, CTX_A1_SLOT
    ctx_load a2, sp, CTX_A2_SLOT
    ctx_load a3, sp, CTX_A3_SLOT
    ctx_load a4, sp, CTX_A4_SLOT
    ctx_load a5, sp, CTX_A5_SLOT
    ctx_load a6, sp, CTX_A6_SLOT
    ctx_load a7, sp, CTX_A7_SLOT
    ctx_load s2, sp, CTX_S2_SLOT
    ctx_load s3, sp, CTX_S3_SLOT
    ctx_load s4, sp, CTX_S4_SLOT
    ctx_load s5, sp, CTX_S5_SLOT
    ctx_load s6, sp, CTX_S6_SLOT
    ctx_load s7, sp, CTX_S7_SLOT
    ctx_load s8, sp, CTX_S8_SLOT
    ctx_load s9, sp, CTX_S9_SLOT
    ctx_load s10, sp, CTX_S10_SLOT
    ctx_load s11, sp, CTX_S11_SLOT
    ctx_load t3, sp, CTX_T3_SLOT
    ctx_load t4, sp, CTX_T4_SLOT
    ctx_load t5, sp, CTX_T5_SLOT
    ctx_load t6, sp, CTX_T6_SLOT
isr_rtn:
    /* 直接从当前内核栈中恢复sp */
    ctx_load sp, sp, CTX_SP_SLOT
    /* 此时, sp指向了之前的栈顶指针 */
    /* 上下文此时已彻底恢复 */
    sret

.globl isr_restore_user
.type isr_restore_user, @function
isr_restore_user:
    /* 从指定线程的内核栈恢复上下文, 用于第一次进入用户态 */
    mv sp, a0
    j isr_ustkr

.globl isr_restore_kernel
.type isr_restore_kernel, @function
isr_restore_kernel:
    /* 从指定线程的内核栈恢复S-Mode上下文, 运行时sscratch必须为0 */
    csrw sscratch, zero
    mv sp, a0
    addi sp, sp, -STACK_SIZE
    j isr_rcsrs

.globl riscv64_kernel_switch
.type riscv64_kernel_switch, @function
riscv64_kernel_switch:
    save_kernel_switch_context a0, 1f
    restore_kernel_switch_context a1
1:
    ret

.globl riscv64_kernel_switch_to_user
.type riscv64_kernel_switch_to_user, @function
riscv64_kernel_switch_to_user:
    save_kernel_switch_context a0, 1f
    mv a0, a1
    j isr_restore_user
1:
    ret
