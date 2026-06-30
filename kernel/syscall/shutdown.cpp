/**
 * @file shutdown.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief shutdown
 * @version alpha-1.0.0
 * @date 2026-06-23
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/model.h>
#include <logger.h>
#include <syscall/shutdown.h>
#include <task/scheduler.h>
#include <task/wait.h>

namespace syscall {
    [[noreturn]]
    void sys_shutdown() noexcept {
        auto *platform = device::DeviceModel::inst().platform();
        if (platform == nullptr) {
            loggers::SYSCALL::FATAL("sys_shutdown 失败: platform 不可用");
            panic("sys_shutdown: platform 不可用");
        }

        auto *driver = platform->shutdown_driver();
        if (driver == nullptr) {
            loggers::SYSCALL::FATAL(
                "sys_shutdown 失败: 未注册 shutdown driver");
            panic("sys_shutdown: 未注册 shutdown driver");
        }

        loggers::SYSCALL::INFO("收到 sys_shutdown 请求, 开始执行平台关机");
        driver->shutdown();
        loggers::SYSCALL::FATAL("sys_shutdown 意外返回");
        panic("sys_shutdown 意外返回");
    }

    [[noreturn]]
    void sys_block_forever() noexcept {
        auto block_wd = wait::alloc_reason();
        while (true) {
            auto block_res = schd::Scheduler::inst().block_current(block_wd);
            if (!block_res.has_value()) {
                loggers::SYSCALL::ERROR("sys_block_forever 失败: %s",
                                        to_cstring(block_res.error()));
            }
        }

        loggers::SYSCALL::FATAL("sys_block_forever 被意外唤醒");
        panic("sys_block_forever 被意外唤醒");
    }
}  // namespace syscall
