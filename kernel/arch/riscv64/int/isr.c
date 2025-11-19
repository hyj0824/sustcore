/**
 * @file isr.c
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 中断处理例程
 * @version alpha-1.0.0
 * @date 2025-11-19
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <arch/riscv64/int/isr.h>
#include <arch/riscv64/int/trap.h>
#include <basec/logger.h>

enum {
    EXCEPTION_INST_MISALIGNED     = 0,   // 指令地址不对齐
    EXCEPTION_INST_ACCESS_FAULT   = 1,   // 指令访问错误
    EXCEPTION_ILLEGAL_INST        = 2,   // 非法指令
    EXCEPTION_BREAKPOINT          = 3,   // 断点
    EXCEPTION_LOAD_MISALIGNED     = 4,   // 加载地址不对齐
    EXCEPTION_LOAD_ACCESS_FAULT   = 5,   // 加载访问错误
    EXCEPTION_STORE_MISALIGNED    = 6,   // 存储地址不对齐
    EXCEPTION_STORE_ACCESS_FAULT  = 7,   // 存储访问错误
    EXCEPTION_ECALL_U             = 8,   // 用户模式环境调用
    EXCEPTION_ECALL_S             = 9,   // 监管模式环境调用
    EXCEPTION_RESERVED_0          = 10,  // 保留
    EXCEPTION_RESERVED_1          = 11,  // 保留
    EXCEPTION_INST_PAGE_FAULT     = 12,  // 指令页错误
    EXCEPTION_LOAD_PAGE_FAULT     = 13,  // 加载页错误
    EXCEPTION_RESERVED_2          = 14,  // 保留
    EXCEPTION_STORE_PAGE_FAULT    = 15,  // 存储页错误
    EXCEPTION_RESERVED_3          = 16,  // 保留
    EXCEPTION_RESERVED_4          = 17,  // 保留
    EXCEPTION_SOFTWARE_CHECK      = 18,  // 软件检查异常
    EXCEPTION_HARDWARE_EEROR      = 19   // 硬件错误
};

void illegal_instruction_handler(umb_t sepc, umb_t stval,
                       InterruptContextRegisterList *reglist_ptr)
{
    log_info("非法指令处理程序: sepc=0x%lx, stval=0x%lx", sepc, stval);

    // 我们可以通过该指令自定义kernel服务
    dword ins = *((dword *)sepc);
    log_info("指令内容: 0x%08x", ins);
    // 这是一个任意的非法指令
    // 被我们选中用于模拟真实指令
    if (ins == 0x000000FF) {
        log_info("自定义Kernel服务: Hello, World!");
    } 
    else if (ins == 0x00FF00FF) {
        log_info("自定义Kernel服务: 计算t0的t1次方, 结果存储到t0中");
        int t0 = reglist_ptr->regs[5 - 1]; // x5 = t0
        int t1 = reglist_ptr->regs[6 - 1]; // x6 = t1
        log_info("计算参数: t0=%d, t1=%d", t0, t1);
        int result = 1;
        for (int i = 0; i < t1; i++) {
            result *= t0;
        }
        reglist_ptr->regs[5 - 1] = result; // x5 = t0
        log_info("计算完成!");
    }
    else {
        log_error("非kernel自定义指令: 0x%08x", ins);
    }

    reglist_ptr->sepc += 4; // 跳过该指令
}

void general_exception(umb_t scause, umb_t sepc, umb_t stval,
                       InterruptContextRegisterList *reglist_ptr)
{
    // 异常信息
    const char *exception_msg[] = {
        "指令地址不对齐",
        "指令访问错误",
        "非法指令",
        "断点",
        "加载地址不对齐",
        "加载访问错误",
        "存储地址不对齐",
        "存储访问错误",
        "用户模式环境调用",
        "监管模式环境调用",
        "保留",
        "保留",
        "指令页错误",
        "加载页错误",
        "保留",
        "存储页错误",
        "保留",
        "保留",
        "软件检查异常",
        "硬件错误"
    };

    // 输出异常类型
    if (scause < sizeof(exception_msg)/sizeof(exception_msg[0])) {
        log_info("发生异常! 类型: %s (%lu)", exception_msg[scause], scause);
    } else {
        log_info("发生异常! 类型: 未知 (%lu)", scause);
    }

    // 输出寄存器状态
    log_info("scause: 0x%lx, sepc: 0x%lx, stval: 0x%lx", scause, sepc, stval);
    log_info("reglist_ptr: 0x%lx", reglist_ptr);

    for (int i = 0; i < 31; i++) {
        log_info("x%d: 0x%lx", i + 1, reglist_ptr->regs[i]);
    }

    log_info("sstatus: 0x%lx", reglist_ptr->sstatus);

    // 输出异常发生特权级
    if ( (reglist_ptr->sstatus >> 8) & 0x1 ) {
        log_info("异常发生在S-Mode");
    } else {
        log_info("异常发生在U-Mode");
    }

    switch (scause) {
    case EXCEPTION_ILLEGAL_INST:
        illegal_instruction_handler(sepc, stval, reglist_ptr);
        break;
    default:
        log_error("无对应解决方案: 0x%lx", scause);
        while (true);
    }
}