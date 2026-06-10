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
#include <bio/request.h>
#include <device/int.h>
#include <device/model.h>
#include <driver/model.h>
#include <driver/rtc/goldfish.h>
#include <driver/serial.h>
#include <driver/virtio/virtio-blk.h>
#include <driver/virtio/virtio.h>
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
#include <string>

extern void register_timekeeper_log_test();

namespace {
    constexpr const char *INITRD_PATH     = "/initrd/";
    constexpr const char *INITMOD_PATH    = "/initrd/init.mod";
    constexpr const char *SETUPMOD_PATH   = "/initrd/setup.mod";
    constexpr const char *DEFAULTMOD_PATH = "/initrd/default.mod";
    constexpr const char *TEST_IMG_PATH   = "/test_img/";
    constexpr const char *TEST_IMG_FEATURE_PATH = "/test_img/feature.mk";
    constexpr size_t FILE_READ_CHUNK_SIZE       = 256;

    devfs::DevFSDriver *g_devfs_driver = nullptr;
    util::owner<RamDiskDevice *> g_initrd_device =
        util::owner<RamDiskDevice *>(nullptr);
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

    Result<std::string> read_vfs_file_text(const char *path) {
        auto file_res = VFS::inst().__debug_open(path);
        if (!file_res.has_value()) {
            loggers::SUSTCORE::ERROR("打开文件失败: path=%s err=%s", path,
                                     to_cstring(file_res.error()));
        }
        propagate(file_res);
        auto *file = file_res.value();
        util::Guard file_guard([file]() { file->destruct(); });

        auto size_res = VFS::inst().size(*file);
        if (!size_res.has_value()) {
            loggers::SUSTCORE::ERROR("获取文件大小失败: path=%s err=%s", path,
                                     to_cstring(size_res.error()));
        }
        propagate(size_res);
        std::string content(size_res.value(), '\0');

        size_t offset = 0;
        while (offset < content.size()) {
            const auto chunk =
                std::min(FILE_READ_CHUNK_SIZE, content.size() - offset);
            auto read_res =
                VFS::inst().read(*file, static_cast<off_t>(offset),
                                 content.data() + offset, chunk);
            if (!read_res.has_value()) {
                loggers::SUSTCORE::ERROR(
                    "读取文件失败: path=%s offset=%u err=%s", path,
                    static_cast<unsigned>(offset), to_cstring(read_res.error()));
            }
            propagate(read_res);
            if (read_res.value() == 0) {
                break;
            }
            offset += read_res.value();
        }
        content.resize(offset);
        return content;
    }

    void log_text_lines(const char *prefix, const std::string &content) {
        size_t line_begin = 0;
        while (line_begin < content.size()) {
            const auto line_end = content.find('\n', line_begin);
            const auto line_len =
                line_end == std::string::npos ? content.size() - line_begin
                                              : line_end - line_begin;
            loggers::SUSTCORE::INFO("%s%.*s", prefix, static_cast<int>(line_len),
                                    content.data() + line_begin);
            if (line_end == std::string::npos) {
                break;
            }
            line_begin = line_end + 1;
        }
        if (content.empty()) {
            loggers::SUSTCORE::INFO("%s", prefix);
        }
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

        g_initrd_device = make_initrd();
        auto devno_res  = blk::BlkManager::inst().register_device(
            util::nnullforce(static_cast<IBlockDeviceOps *>(
                g_initrd_device.get())));
        if (!devno_res.has_value()) {
            propagate_return(devno_res);
        }

        auto mount_res = vfs.mount("tarfs", devno_res.value(), INITRD_PATH,
                                   MountFlags::NONE, nullptr);
        propagate(mount_res);
        mount_res = vfs.mount("devfs", "/sys/", nullptr);
        propagate(mount_res);
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
        auto block_wd  = wait::alloc_reason();
        auto block_res = schd::Scheduler::inst().block_current(block_wd);
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
