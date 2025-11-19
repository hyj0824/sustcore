/**
 * @file isr.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 中断处理例程
 * @version alpha-1.0.0
 * @date 2025-11-19
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <arch/riscv64/int/exception.h>
#include <arch/riscv64/int/trap.h>

/**
 * @brief 通用异常处理程序
 *
 * @param scause scause寄存器 存放异常原因
 * @param sepc   sepc寄存器   存放异常发生时的指令地址
 * @param stval  stval寄存器  存放异常相关的地址或信息
 * @param reglist_ptr 指向中断上下文寄存器列表的指针
 */
void general_exception(umb_t scause, umb_t sepc, umb_t stval,
                       InterruptContextRegisterList *reglist_ptr);