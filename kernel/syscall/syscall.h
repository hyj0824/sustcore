/**
 * @file syscall.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 系统调用
 * @version alpha-1.0.0
 * @date 2026-05-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sus/coroutine.h>
#include <sus/nonnull.h>
#include <sus/types.h>

struct Riscv64Context;
namespace task {
    struct TCB;
}

namespace syscall {
    struct ArgPack {
        b64 syscall_number;
        b64 capidx;
        constexpr static size_t ARGS_SIZE = 5;
        b64 args[ARGS_SIZE];
    };

    struct RetPack {
        bool processed;
        b64 ret0;
        b64 ret1;
    };

    /**
     * @brief 系统调用入口函数
     *
     * @param ctx 触发系统调用时保存的上下文; 入口内部负责读取参数并写回返回值
     * @return util::cotask<RetPack>
     * 系统调用结果包, 包含是否成功处理、返回值0和返回值1
     * @note 该函数为系统调用的统一入口, 负责根据系统调用号分发到具体的系统调用处理函数. 
     */
    util::cotask<RetPack> entrance(const util::nonnull<Riscv64Context *> &ctx,
                                   const util::nonnull<task::TCB *> &tcb);
}  // namespace syscall
