/**
 * @file main.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 主文件
 * @version alpha-1.0.0
 * @date 2026-04-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <kmod/syscall.h>

#include <cstdio>

int kmod_main() {
    printf("进入 init 模块!\n");

    // sys_create_process("/initrd/test_fork.mod", nullptr, 0, SCHED_CLASS_RR);
    sys_create_process("/initrd/test_thread.mod", nullptr, 0, SCHED_CLASS_RR);
    // sys_create_process("/initrd/test_endpoint_master.mod", nullptr, 0, SCHED_CLASS_FCFS);
    // sys_create_process("/initrd/test_call_service.mod", nullptr, 0, SCHED_CLASS_FCFS);
    sys_create_process("/initrd/test_rpc_server.mod", nullptr, 0, SCHED_CLASS_FCFS);

    printf("init: 启动完成, 退出\n");
    exit(0);
    return 0;
}

