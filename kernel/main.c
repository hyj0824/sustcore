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

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#include <basec/logger.h>

#include <mem/alloc.h>
#include <mem/pmm.h>

#include <sus/arch.h>
#include <sus/boot.h>

/**
 * @brief 内核主函数
 * 
 * @return int 
 */
int main(void) {
    log_info("Hello RISCV World!");

    while(true);

    return 0;
}

/**
 * @brief 初始化
 * 
 */
void init(void) {
    kputs("\n");

    init_logger(kputs, "SUSTCore");

    log_info("初始化内存分配器...");
    init_allocator();

    arch_init();

    pmm_init(nullptr);
}

/**
 * @brief 收尾工作
 * 
 */
void terminate(void) {
    arch_terminate();
    log_info("内核已关闭!");
    while(true);
}

/**
 * @brief 内核启动函数
 *
 */
void kernel_setup(void) {
    init();

    main();

    terminate();
}