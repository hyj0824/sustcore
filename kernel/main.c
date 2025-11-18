/**
 * @file main.c
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 内核Main函数
 * @version alpha-1.0.0
 * @date 2025-11-17
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <basec/logger.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <sbi/sbi.h>
#include <libfdt.h>
#include <arch/riscv64/int/exception.h>

int kputchar(int ch) {
    sbi_dbcn_console_write_byte((char)ch);
    return ch;
}

int kputs(const char *str) {
    int len = strlen(str);
    sbi_dbcn_console_write((umb_t)len, (const void*)str);
    return len;
}

umb_t hart_id, dtb_ptr;

//------------------ 调试libfdt使用-----------

/* 基础验证 */
int fdt_check_initial(void *fdt) {
    /* 检查魔数 */
    if (fdt_magic(fdt) != FDT_MAGIC) {
        return -FDT_ERR_BADMAGIC;
    }
    
    /* 检查版本兼容性 */
    if (fdt_version(fdt) < FDT_FIRST_SUPPORTED_VERSION) {
        return -FDT_ERR_BADVERSION;
    }
    
    /* 完整检查 */
    return fdt_check_header(fdt);
}

/** 遍历节点 */
void traverse_nodes(void *fdt) {
    int node, depth = 0;
    
    /* 从根节点开始遍历 */
    for (node = fdt_next_node(fdt, -1, &depth);
         node >= 0;
         node = fdt_next_node(fdt, node, &depth)) {
        
        const char *name = fdt_get_name(fdt, node, NULL);
        log_debug("Node: %s (depth: %d)", name, depth);
    }
}

//------------------ 调试libfdt使用-----------

//------------------ 调试异常处理程序 --------

// 触发非法指令异常
__attribute__((noinline))
int trigger_illegal_instruction(void) {
    asm volatile (".word 0x00000000");  // 全零是非法指令
    return -1;
}

//------------------ 调试异常处理程序 --------

/**
 * @brief 内核主函数
 * 
 * @return int 
 */
int main(void) {
    log_info("Hello RISCV World!");
    log_info("Hart ID: %u", (unsigned int)hart_id);
    log_info("DTB Ptr: 0x%016lx", (unsigned long)dtb_ptr);

    log_info("开始验证设备树...");

    void *fdt = (void *)dtb_ptr;
    int ret = fdt_check_initial(fdt);
    if (ret != 0) {
        log_error("设备树校验失败: %d", ret);
        return -1;
    }

    log_info("设备树校验成功!");
    log_info("开始遍历设备树节点...");

    traverse_nodes(fdt);

    log_info("设备树节遍历完成!");

    log_info("开始测试非法指令异常处理...");
    init_ivt();
    int a = trigger_illegal_instruction();
    log_info("非法指令异常测试结果: %d", a);

    return 0;
}

/**
 * @brief 初始化
 * 
 */
void init(void) {
    kputs("\n");
    init_logger(kputs, "SUSTCore");
}

/**
 * @brief 收尾工作
 * 
 */
void terminate(void) {
    SBIRet ret;
    ret = sbi_shutdown();
    if (ret.error) {
        log_error("关机失败!");
    } 
    
    log_error("何意味?关机成功了?!你又是怎么溜达到这来的?!!");
    while(true);
}

/**
 * @brief 内核启动函数
 *
 */
void c_setup(void) {
    init();

    main();

    terminate();
}