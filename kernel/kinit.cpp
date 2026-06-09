/**
 * @file kinit.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief kinit 线程
 * @version alpha-1.0.0
 * @date 2026-06-09
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <bio/blk.h>
#include <bio/block.h>
#include <device/int.h>
#include <device/model.h>
#include <driver/model.h>
#include <driver/rtc/goldfish.h>
#include <driver/serial.h>
#include <env.h>
#include <exe/task.h>
#include <kinit.h>
#include <logger.h>
#include <symbols.h>
#include <sus/logger.h>
#include <sus/owner.h>
#include <sus/units.h>
#include <task/scheduler.h>
#include <task/task.h>
#include <task/wait.h>
#include <vfs/device.h>
#include <vfs/ops.h>
#include <vfs/tarfs.h>
#include <vfs/vfs.h>

#include <cassert>
#include <cstring>

extern void register_timekeeper_log_test();

namespace {
    constexpr const char *INITRD_PATH     = "/initrd/";
    constexpr const char *INITMOD_PATH    = "/initrd/init.mod";
    constexpr const char *SETUPMOD_PATH   = "/initrd/setup.mod";
    constexpr const char *DEFAULTMOD_PATH = "/initrd/default.mod";

    devfs::DevFSDriver *g_devfs_driver = nullptr;
    driver::GoldfishRTC::AlarmHandler ticker;

    util::owner<RamDiskDevice *> make_initrd() {
        auto e_initrd_ptr = reinterpret_cast<char *>(&e_initrd);
        auto s_initrd_ptr = reinterpret_cast<char *>(&s_initrd);
        size_t sz         = e_initrd_ptr - s_initrd_ptr;
        auto device       = util::owner(new RamDiskDevice(&s_initrd, sz, 1));
        loggers::SUSTCORE::INFO("initrd大小为 %u KB =  %u MB", sz / 1024,
                                sz / 1024 / 1024);
        return device;
    }

    Result<void> init_vfs() {
        blk::BlkManager::init();
        VFS::init();
        auto &vfs = VFS::inst();

        auto tarfs        = util::owner(new tarfs::TarFSDriver());
        auto register_res = vfs.register_fs(tarfs);
        propagate(register_res);
        auto devfs_driver = util::owner(new devfs::DevFSDriver());
        g_devfs_driver    = devfs_driver.get();
        register_res      = vfs.register_fs(std::move(devfs_driver));
        propagate(register_res);

        auto initrd_device = make_initrd();
        auto devno_res     = blk::BlkManager::inst().register_device(
            util::owner<IBlockDeviceOps *>(initrd_device.get()));
        if (!devno_res.has_value()) {
            propagate_return(devno_res);
        }
        initrd_device = util::owner<RamDiskDevice *>(nullptr);

        auto mount_res = vfs.mount("tarfs", devno_res.value(), INITRD_PATH,
                                   MountFlags::NONE, nullptr);
        propagate(mount_res);
        mount_res = vfs.mount("devfs", "/sys/", nullptr);
        propagate(mount_res);
        void_return();
    }

    Result<void> init_runtime_drivers() {
        auto devices1 =
            device::DeviceModel::inst().find_devices_by_compatible("ns16550a");
        loggers::SUSTCORE::INFO("兼容 ns16550a 的设备数量: %u",
                                static_cast<unsigned>(devices1.size()));
        if (devices1.size() > 0) {
            auto *serial_device = devices1[0];
            auto create_res =
                driver::DriverModel::inst().create_driver(serial_device);
            if (!create_res.has_value()) {
                loggers::SUSTCORE::ERROR("为 ns16550a 设备创建驱动失败: %s",
                                         to_cstring(create_res.error()));
            } else {
                loggers::SUSTCORE::INFO("已为 ns16550a 设备创建驱动");
                auto *driver =
                    static_cast<driver::SerialDevice *>(create_res.value());
                driver->write("Hello, Sustcore!\n", strlen("Hello, Sustcore!\n"));
                if (g_devfs_driver != nullptr) {
                    auto devfs_res = g_devfs_driver->mounted_superblock();
                    if (!devfs_res.has_value()) {
                        loggers::SUSTCORE::ERROR("获取 DevFS 超级块失败: %s",
                                                 to_cstring(devfs_res.error()));
                    } else {
                        auto mount_res =
                            driver->mount(*devfs_res.value(), nullptr);
                        if (!mount_res.has_value()) {
                            loggers::SUSTCORE::ERROR(
                                "挂载串口设备文件失败: %s",
                                to_cstring(mount_res.error()));
                        }
                    }

                    auto serial_file =
                        VFS::inst().__debug_open("/sys/serial/serial");
                    if (!serial_file.has_value()) {
                        loggers::SUSTCORE::ERROR(
                            "调试打开串口设备文件失败: %s",
                            to_cstring(serial_file.error()));
                    } else {
                        auto write_res = VFS::inst().write(
                            *serial_file.value(), 0, "Debug Hello!\n",
                            strlen("Debug Hello!\n"));
                        if (!write_res.has_value()) {
                            loggers::SUSTCORE::ERROR(
                                "调试写入串口设备文件失败: %s",
                                to_cstring(write_res.error()));
                        }
                    }
                }
            }
        }

        auto devices2 = device::DeviceModel::inst().find_devices_by_compatible(
            "google,goldfish-rtc");
        loggers::SUSTCORE::INFO("兼容 google,goldfish-rtc 的设备数量: %u",
                                static_cast<unsigned>(devices2.size()));

        if (devices2.size() > 0) {
            auto *rtc_device = devices2[0];
            auto create_res =
                driver::DriverModel::inst().create_driver(rtc_device);
            if (!create_res.has_value()) {
                loggers::SUSTCORE::ERROR(
                    "为 google,goldfish-rtc 设备创建驱动失败: %s",
                    to_cstring(create_res.error()));
            } else {
                loggers::SUSTCORE::INFO("已为 google,goldfish-rtc 设备创建驱动");
                auto *driver =
                    static_cast<driver::GoldfishRTC *>(create_res.value());
                auto time = driver->read_time();
                auto ft   = units::rt_time::from_time(time).to_formatted_time();
                loggers::SUSTCORE::INFO(
                    "当前 RTC 时间: %04lld-%02lld-%02lld %02lld:%02lld:%02lld",
                    static_cast<long long>(ft.year),
                    static_cast<long long>(ft.month),
                    static_cast<long long>(ft.day),
                    static_cast<long long>(ft.hour),
                    static_cast<long long>(ft.minute),
                    static_cast<long long>(ft.second));
                auto alarm_time = time + units::time::from_seconds(2);

                ticker = [driver](units::time now) {
                    auto alarm_ft =
                        units::rt_time::from_time(now).to_formatted_time();
                    loggers::SUSTCORE::INFO(
                        "Goldfish RTC alarm 触发: %04lld-%02lld-%02lld "
                        "%02lld:%02lld:%02lld",
                        static_cast<long long>(alarm_ft.year),
                        static_cast<long long>(alarm_ft.month),
                        static_cast<long long>(alarm_ft.day),
                        static_cast<long long>(alarm_ft.hour),
                        static_cast<long long>(alarm_ft.minute),
                        static_cast<long long>(alarm_ft.second));
                    auto next_alarm_time = now + units::time::from_seconds(2);
                    driver->set_alarm(next_alarm_time, ticker);
                };

                driver->set_alarm(alarm_time, ticker);
            }
        }
        void_return();
    }

    Result<void> load_runtime_init() {
        auto load_res = task::TaskManager::inst().load_init(INITMOD_PATH);
        if (!load_res.has_value()) {
            loggers::SUSTCORE::ERROR("加载初始进程失败! 错误码: %s",
                                     to_cstring(load_res.error()));
            propagate_return(load_res);
        }
        auto &task = load_res.value();
        assert(task->threads.size() == 1);
        auto *init_tcb = &task->threads.front();
        if (!schd::Scheduler::inst().wakeup_new(init_tcb)) {
            loggers::SUSTCORE::ERROR("唤醒初始进程失败");
            unexpect_return(ErrCode::CREATION_FAILED);
        }
        void_return();
    }

    [[noreturn]]
    void block_kinit_forever() {
        auto reason    = task::wait::alloc_reason();
        auto block_res = schd::Scheduler::inst().block_current(reason);
        if (!block_res.has_value()) {
            loggers::SUSTCORE::FATAL("阻塞 kinit 失败: %s",
                                     to_cstring(block_res.error()));
            panic("阻塞 kinit 失败");
        }
        panic("kinit 被意外唤醒");
    }
}  // namespace

void kinit_runtime_entry() {
    loggers::SUSTCORE::INFO("进入 kinit 运行时初始化阶段");
    Interrupt::sti();

    auto init_res = init_vfs();
    if (!init_res.has_value()) {
        loggers::SUSTCORE::FATAL("kinit 初始化 VFS 失败: %s",
                                 to_cstring(init_res.error()));
        panic("kinit 初始化 VFS 失败");
    }

    init_res = init_runtime_drivers();
    if (!init_res.has_value()) {
        loggers::SUSTCORE::FATAL("kinit 初始化运行时驱动失败: %s",
                                 to_cstring(init_res.error()));
        panic("kinit 初始化运行时驱动失败");
    }

#ifdef __CONF_KERNEL_TIMEKEEPER_TEST
    register_timekeeper_log_test();
#endif

    init_res = load_runtime_init();
    if (!init_res.has_value()) {
        loggers::SUSTCORE::FATAL("kinit 加载用户 init 失败: %s",
                                 to_cstring(init_res.error()));
        panic("kinit 加载用户 init 失败");
    }

    block_kinit_forever();
}
