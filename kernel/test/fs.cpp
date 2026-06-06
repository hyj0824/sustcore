/**
 * @file fs.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 文件系统测试
 * @version alpha-1.0.0
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <bio/buffer.h>
#include <bio/blk.h>
#include <logger.h>
#include <mem/alloc.h>
#include <object/vfile.h>
#include <symbols.h>
#include <test/fs.h>
#include <vfs/tarfs.h>
#include <vfs/vfs.h>

#include <cstdint>

namespace test::fs {

    static RamDiskDevice* make_initrd();

    static RamDiskDevice* _block_dev = nullptr;
    static void ensure_block_dev();
    static Result<size_t> register_device(IBlockDeviceOps *device);
    static Result<void> unregister_device(size_t devno);

    class CaseBlkManagerRegisterLookup : public TestCase {
    public:
        CaseBlkManagerRegisterLookup()
            : TestCase("BlkManager 注册并提供设备与缓存") {}

        void _run(void* env) const noexcept override {
            auto* data = new char[64];
            tassert(data != nullptr, "应能分配块设备后端内存");
            memset(data, 0, 64);

            auto dev = util::owner<IBlockDeviceOps *>(
                new RamDiskDevice(data, 64, 1));
            tassert(dev.get() != nullptr, "应能创建测试 RamDisk 设备");

            auto reg_res =
                blk::BlkManager::inst().register_device(std::move(dev));
            tassert(reg_res.has_value(), "注册块设备应成功");
            tassert(dev.get() == nullptr, "注册后所有权应被 BlkManager 接管");

            size_t devno = reg_res.value();
            auto lookup_res = blk::BlkManager::inst().lookup(devno);
            tassert(lookup_res.has_value() && lookup_res.value() != nullptr,
                    "应能按设备号取回块设备");

            auto cache_res = blk::BlkManager::inst().lookup_cache(devno);
            tassert(cache_res.has_value() && cache_res.value() != nullptr,
                    "注册时应自动创建 BufferCache");

            auto id_res =
                blk::BlkManager::inst().find_device_id(lookup_res.value());
            tassert(id_res.has_value() && id_res.value() == devno,
                    "应能从设备指针反查 devno");

            auto dup_res = blk::BlkManager::inst().register_device(
                util::owner<IBlockDeviceOps *>(lookup_res.value()));
            tassert(!dup_res.has_value() &&
                        dup_res.error() == ErrCode::KEY_DUPLICATED,
                    "重复注册同一设备指针应失败");

            auto unreg_res = blk::BlkManager::inst().unregister_device(devno);
            tassert(unreg_res.has_value(), "注销块设备应成功");

            auto missing_lookup = blk::BlkManager::inst().lookup(devno);
            tassert(!missing_lookup.has_value() &&
                        missing_lookup.error() == ErrCode::ENTRY_NOT_FOUND,
                    "注销后设备查找应失败");
        }
    };

    class CaseBufferCacheReadWriteSync : public TestCase {
    public:
        CaseBufferCacheReadWriteSync()
            : TestCase("BufferCache 读写与同步行为") {}

        void _run(void* env) const noexcept override {
            ensure_block_dev();
            auto* dev = _block_dev;
            tassert(dev != nullptr, "应能创建测试块设备");

            blk::BufferCache cache(dev, 1);
            auto first_res = cache.get_buffer(0);
            tassert(first_res.has_value(), "首次获取块缓存应成功");
            tassert(first_res.value().is_valid(), "首次读取后缓存应有效");

            char raw[16]  = {};
            auto read_len = first_res.value().read(0, raw, sizeof(raw));
            tassert(read_len == sizeof(raw), "应能从缓存读取整个前缀");
            tassert(raw[0] == 'A' && raw[1] == 'B', "缓存内容应来自底层块设备");

            const char* payload = "cache-write";
            auto written = first_res.value().write(8, payload, strlen(payload));
            tassert(written == strlen(payload), "写入缓存的长度应正确");
            tassert(first_res.value().is_dirty(), "写入后缓存应变脏");

            auto sync_res = cache.sync(first_res.value());
            tassert(sync_res.has_value(), "同步单个缓存块应成功");
            tassert(!first_res.value().is_dirty(), "同步后脏标记应被清除");

            auto second_res = cache.get_buffer(0);
            tassert(second_res.has_value(), "再次获取同一块缓存应成功");
            tassert(second_res.value().get() == first_res.value().get(),
                    "命中缓存时应返回同一块缓存对象");

            char verify[64]    = {};
            auto verify_blocks = dev->read_blocks(0, verify, 1);
            tassert(verify_blocks.has_value() && verify_blocks.value() == 1,
                    "应能直接从设备读回完整块");
            tassert(memcmp(verify + 8, payload, strlen(payload)) == 0,
                    "同步后的设备内容应与缓存一致");
        }
    };

    class CaseBufferCacheTidy : public TestCase {
    public:
        CaseBufferCacheTidy() : TestCase("BufferCache tidy 回收空闲块") {}

        void _run(void* env) const noexcept override {
            ensure_block_dev();
            auto* dev = _block_dev;
            tassert(dev != nullptr, "应能创建测试块设备");
            blk::BufferCache cache(dev, 2);
            {
                auto buf0 = cache.get_buffer(0);
                tassert(buf0.has_value(), "应能获取第 0 块缓存");
                const char* payload0 = "dirty0";
                auto written0 =
                    buf0.value().write(0, payload0, strlen(payload0));
                tassert(written0 == strlen(payload0), "应能写脏第 0 块");
            }
            {
                auto buf1 = cache.get_buffer(1);
                tassert(buf1.has_value(), "应能获取第 1 块缓存");
                const char* payload1 = "dirty1";
                auto written1 =
                    buf1.value().write(4, payload1, strlen(payload1));
                tassert(written1 == strlen(payload1), "应能写脏第 1 块");
            }

            auto tidy_res = cache.tidy();
            tassert(tidy_res.has_value(), "tidy 应能成功回写并回收空闲缓存");

            auto reload0 = cache.get_buffer(0);
            tassert(reload0.has_value(), "tidy 后仍应能重新加载第 0 块");
            char verify0[16] = {};
            auto read0 = reload0.value().read(0, verify0, sizeof(verify0));
            tassert(read0 == sizeof(verify0), "应能重新读取第 0 块");
            tassert(memcmp(verify0, "dirty0", strlen("dirty0")) == 0,
                    "第 0 块写回内容应保留");

            auto reload1 = cache.get_buffer(1);
            tassert(reload1.has_value(), "tidy 后仍应能重新加载第 1 块");
            char verify1[16] = {};
            auto read1 = reload1.value().read(4, verify1, sizeof(verify1) - 4);
            tassert(read1 == sizeof(verify1) - 4,
                    "应能重新读取第 1 块偏移内容");
            tassert(memcmp(verify1, "dirty1", strlen("dirty1")) == 0,
                    "第 1 块写回内容应保留");
        }
    };

    class CaseMountOpenRead : public TestCase {
    public:
        CaseMountOpenRead() : TestCase("VFS 挂载 initrd 并读取 license") {}

        void _run(void* env) const noexcept override {
            auto& vfs = VFS::inst();

            RamDiskDevice* initrd = make_initrd();
            tassert(initrd != nullptr, "应能成功创建 initrd RamDisk 设备");
            auto devno_res = register_device(initrd);
            tassert(devno_res.has_value(), "应能注册 initrd 块设备");

            action("挂载 tarfs 到根目录 /");
            auto mount_res =
                vfs.mount("tarfs", devno_res.value(), "/", MountFlags::NONE,
                          nullptr);
            tassert(mount_res.has_value(), "应能将 tarfs 挂载到根目录 /");

            action("打开 initrd 中的 /license 文件");
            auto open_res = vfs.__debug_open("/license");
            tassert(open_res.has_value(), "应能成功打开 /license 文件");

            auto* cap = new cap::Capability(open_res.value(), perm::allperm());
            cap::VFileObject fop(util::nnullforce(cap));

            auto file_size_res = fop.size();
            tassert(file_size_res.has_value() && file_size_res.value() > 0,
                    "license 文件大小应大于 0");

            uint8_t head[32] = {0};
            auto read_res    = fop.read(0, head, sizeof(head));
            tassert(read_res.has_value() && read_res.value() > 0,
                    "应能成功读取 license 文件前缀");

            bool non_zero = false;
            for (size_t i = 0; i < read_res.value(); ++i) {
                non_zero |= head[i] != 0;
            }
            tassert(non_zero, "读取到的文件内容不应全为 0");

            delete cap;
            tassert(true, "应能成功关闭 /license 文件能力");

            auto umount_res = vfs.umount("/");
            tassert(umount_res.has_value(), "卸载根目录 / 应成功");
            auto unreg_res = unregister_device(devno_res.value());
            tassert(unreg_res.has_value(), "卸载后注销 initrd 设备应成功");
        }
    };

    class CaseMountBusyUmount : public TestCase {
    public:
        CaseMountBusyUmount() : TestCase("VFS 忙状态阻止卸载") {}

        void _run(void* env) const noexcept override {
            auto& vfs = VFS::inst();

            RamDiskDevice* initrd = make_initrd();
            tassert(initrd != nullptr, "应能成功创建 initrd RamDisk 设备");
            auto devno_res = register_device(initrd);
            tassert(devno_res.has_value(), "应能注册 initrd 块设备");

            auto mount_res =
                vfs.mount("tarfs", devno_res.value(), "/", MountFlags::NONE,
                          nullptr);
            tassert(mount_res.has_value(), "应能将 tarfs 挂载到根目录 /");

            auto open_res = vfs.__debug_open("/license");
            tassert(open_res.has_value(), "第一次打开 /license 应成功");

            auto open_res2 = vfs.__debug_open("/license");
            tassert(open_res2.has_value(), "第二次打开 /license 应成功");

            auto* cap1 = new cap::Capability(open_res.value(), perm::allperm());
            auto* cap2 =
                new cap::Capability(open_res2.value(), perm::allperm());

            action("文件仍在打开时卸载, 应返回 BUSY");
            auto busy_umount = vfs.umount("/");
            tassert(!busy_umount.has_value() &&
                        busy_umount.error() == ErrCode::BUSY,
                    "有文件打开时卸载应被拒绝 (BUSY)");

            delete cap2;
            tassert(true, "应能成功关闭第二个 /license 文件能力");

            action("仍有一个访问器打开时再次卸载, 仍应 BUSY");
            busy_umount = vfs.umount("/");
            tassert(!busy_umount.has_value() &&
                        busy_umount.error() == ErrCode::BUSY,
                    "仍有文件打开时卸载应被拒绝 (BUSY)");

            delete cap1;
            tassert(true, "应能成功关闭第一个 /license 文件能力");

            auto umount_res = vfs.umount("/");
            tassert(umount_res.has_value(), "释放所有访问器后卸载应成功");
            auto unreg_res = unregister_device(devno_res.value());
            tassert(unreg_res.has_value(), "卸载后注销 initrd 设备应成功");
        }
    };

    class CaseOpenMissingFile : public TestCase {
    public:
        CaseOpenMissingFile() : TestCase("VFS 打开不存在文件应失败") {}

        void _run(void* env) const noexcept override {
            auto& vfs = VFS::inst();

            RamDiskDevice* initrd = make_initrd();
            tassert(initrd != nullptr, "应能成功创建 initrd RamDisk 设备");
            auto devno_res = register_device(initrd);
            tassert(devno_res.has_value(), "应能注册 initrd 块设备");

            auto mount_res =
                vfs.mount("tarfs", devno_res.value(), "/", MountFlags::NONE,
                          nullptr);
            tassert(mount_res.has_value(), "应能将 tarfs 挂载到根目录 /");

            auto missing_open = vfs.__debug_open("/this_file_should_not_exist");
            tassert(!missing_open.has_value(), "打开不存在文件应失败");

            auto umount_res = vfs.umount("/");
            tassert(umount_res.has_value(), "卸载根目录 / 应成功");
            auto unreg_res = unregister_device(devno_res.value());
            tassert(unreg_res.has_value(), "卸载后注销 initrd 设备应成功");
        }
    };

    class CaseMountParamValidation : public TestCase {
    public:
        CaseMountParamValidation() : TestCase("VFS 挂载参数与重复挂载检查") {}

        void _run(void* env) const noexcept override {
            auto& vfs = VFS::inst();

            RamDiskDevice* initrd = make_initrd();
            tassert(initrd != nullptr, "应能成功创建 initrd RamDisk 设备");
            auto devno_res = register_device(initrd);
            tassert(devno_res.has_value(), "应能注册 initrd 块设备");

            action("挂载未注册文件系统应失败");
            auto invalid_mount =
                vfs.mount("unknownfs", devno_res.value(), "/", MountFlags::NONE,
                          nullptr);
            tassert(!invalid_mount.has_value() &&
                        invalid_mount.error() == ErrCode::INVALID_PARAM,
                    "未注册文件系统的挂载应被拒绝");

            auto mount_res =
                vfs.mount("tarfs", devno_res.value(), "/", MountFlags::NONE,
                          nullptr);
            tassert(mount_res.has_value(),
                    "首次将 tarfs 挂载到根目录 / 应成功");

            action("同一挂载点重复挂载应失败");
            auto duplicate_mount =
                vfs.mount("tarfs", devno_res.value(), "/", MountFlags::NONE,
                          nullptr);
            tassert(!duplicate_mount.has_value() &&
                        duplicate_mount.error() == ErrCode::INVALID_PARAM,
                    "同一挂载点重复挂载应被拒绝");

            auto umount_res = vfs.umount("/");
            tassert(umount_res.has_value(), "卸载根目录 / 应成功");
            auto unreg_res = unregister_device(devno_res.value());
            tassert(unreg_res.has_value(), "卸载后注销 initrd 设备应成功");
        }
    };

    static RamDiskDevice* make_initrd() {
        size_t sz             = (char*)&e_initrd - (char*)&s_initrd;
        RamDiskDevice* device = new RamDiskDevice(&s_initrd, sz, 1);
        return device;
    }

    static void ensure_block_dev() {
        if (_block_dev == nullptr) {
            auto* data = new char[128];
            if (data == nullptr) {
                return;
            }
            memset(data, 0, 128);
            memcpy(data, "ABCDEFGH", 8);
            memcpy(data + 64, "ijklmnop", 8);
            _block_dev = new RamDiskDevice(data, 64, 2);
        }
    }

    static Result<size_t> register_device(IBlockDeviceOps *device) {
        return blk::BlkManager::inst().register_device(
            util::owner<IBlockDeviceOps *>(device));
    }

    static Result<void> unregister_device(size_t devno) {
        return blk::BlkManager::inst().unregister_device(devno);
    }

    void collect_tests(TestFramework& framework) {
        auto cases = util::ArrayList<TestCase*>();
        cases.push_back(new CaseBlkManagerRegisterLookup());
        cases.push_back(new CaseBufferCacheReadWriteSync());
        cases.push_back(new CaseBufferCacheTidy());
        cases.push_back(new CaseMountOpenRead());
        cases.push_back(new CaseMountBusyUmount());
        cases.push_back(new CaseOpenMissingFile());
        cases.push_back(new CaseMountParamValidation());
        framework.add_category(new TestCategory("fs", std::move(cases)));
    }
}  // namespace test::fs
