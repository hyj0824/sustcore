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
#include <sustcore/errcode.h>

struct Riscv64Context;
namespace task {
    struct TCB;
    struct PCB;
    struct SyscallContext;
}

namespace syscall {
    class UBuffer;

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
     * @brief 当前正在由专用协程线程执行的 syscall 上下文.
     *
     * @return task::SyscallContext* 当前上下文.
     */
    [[nodiscard]]
    task::SyscallContext *active_context() noexcept;

    /**
     * @brief 设置当前专用协程线程绑定的 syscall 上下文.
     *
     * @param context 要绑定的上下文.
     */
    void set_active_context(task::SyscallContext *context) noexcept;

    /**
     * @brief 获取当前 syscall 所属线程.
     *
     * @return Result<task::TCB *> 成功时返回线程指针.
     */
    [[nodiscard]]
    Result<task::TCB *> current_tcb() noexcept;

    /**
     * @brief 获取当前 syscall 所属进程.
     *
     * @return Result<task::PCB *> 成功时返回进程指针.
     */
    [[nodiscard]]
    Result<task::PCB *> current_pcb() noexcept;

    const char *name_of(b64 sysno);

    /**
     * @brief 判断给定 syscall 是否属于可挂起的协程路径.
     *
     * @param sysno syscall 编号.
     * @return true 该 syscall 可能挂起.
     * @return false 该 syscall 应同步完成.
     */
    [[nodiscard]]
    bool is_suspendable_syscall(b64 sysno) noexcept;

    /**
     * @brief 同步 syscall 分发入口.
     *
     * @param tcb 当前系统调用所属线程.
     * @return RetPack 同步执行结果.
     */
    [[nodiscard]]
    RetPack dispatch_sync(util::nonnull<task::TCB *> tcb);

    /**
     * @brief 可挂起 syscall 分发入口.
     *
     * @param tcb 当前系统调用所属线程.
     * @return util::cotask<void> syscall 协程任务.
     */
    [[nodiscard]]
    util::cotask<void> dispatch_async(util::nonnull<task::TCB *> tcb);
}  // namespace syscall
