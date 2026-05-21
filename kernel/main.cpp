/**
 * @file main.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 内核Main函数
 * @version alpha-1.0.0
 * @date 2025-11-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include <arch/riscv64/csr.h>
#include <arch/riscv64/description.h>
#include <arch/riscv64/device/fdt_helper.h>
#include <arch/riscv64/mem/sv39.h>
#include <arch/trait.h>
#include <cap/capability.h>
#include <cap/cholder.h>
#include <cap/permission.h>
#include <device/block.h>
#include <env.h>
#include <exe/elfloader.h>
#include <exe/task.h>
#include <logger.h>
#include <mem/alloc.h>
#include <mem/gfp.h>
#include <mem/kaddr.h>
#include <mem/slub.h>
#include <mem/vma.h>
#include <sus/logger.h>
#include <sus/nonnull.h>
#include <sus/path.h>
#include <sus/raii.h>
#include <sus/tree.h>
#include <sus/types.h>
#include <sustcore/addr.h>
#include <symbols.h>
#include <task/scheduler.h>
#include <task/task.h>
#include <task/wait.h>
#include <test/framework.h>
#include <test/kthread.h>
#include <vfs/ops.h>
#include <vfs/tarfs.h>
#include <vfs/vfs.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace env {
    static Environment _env;
    static bool _env_initialized = false;

    bool initialized() {
        return _env_initialized;
    }

    Environment &inst() {
        if (!initialized()) {
            panic("Environment 未初始化!");
        }
        return _env;
    }

    void construct() {
        // call the constructor here
        new (&_env) Environment();
        _env_initialized = true;
    }
}  // namespace env

namespace key {
    using namespace env::key;
    struct main : public tmm, meminfo {
    public:
        main() = default;
    };
}  // namespace key

// path
PhyAddr kernel_root                   = PhyAddr::null;
constexpr const char *INITRD_PATH     = "/initrd/";
constexpr const char *INITMOD_PATH    = "/initrd/init.mod";
constexpr const char *SETUPMOD_PATH   = "/initrd/setup.mod";
constexpr const char *DEFAULTMOD_PATH = "/initrd/default.mod";

util::owner<RamDiskDevice *> make_initrd() {
    auto e_initrd_ptr = reinterpret_cast<char *>(&e_initrd);
    auto s_initrd_ptr = reinterpret_cast<char *>(&s_initrd);
    size_t sz         = e_initrd_ptr - s_initrd_ptr;
    auto device       = util::owner(new RamDiskDevice(&s_initrd, sz, 1));
    loggers::SUSTCORE::INFO("initrd大小为 %u KB =  %u MB", sz / 1024,
                            sz / 1024 / 1024);
    return device;
}

void kernel_paging_setup() {
    [[maybe_unused]]
    constexpr KernelStage STAGE = KernelStage::PRE_INIT;
    auto &e                     = env::inst();
    // 创建内核页表管理器
    auto gfp_res                = GFP::get_free_page<STAGE>(1);
    if (!gfp_res.has_value()) {
        loggers::SUSTCORE::ERROR("无法为内核页表分配物理页");
        while (true);
    }

    PhyAddr pgd = gfp_res.value();
    EarlyPageMan::make_root(pgd);
    kernel_root = pgd;
    EarlyPageMan kernelman(kernel_root);

    ker_paddr::init();
    ker_paddr::mapping_kernel_areas(kernelman);

    // 对[0, uppm)进行恒等映射
    size_t sz = e.meminfo().uppm - e.meminfo().lowpm;
    kernelman.map_range<true>(e.meminfo().lowvm, e.meminfo().lowpm, sz,
                              EarlyPageMan::rwx(true, true, true), false, true);

    kernelman.switch_root();
    kernelman.flush_tlb();
}

Result<void> init_vfs() {
    // 构造VFS
    VFS::init();
    auto &vfs = VFS::inst();

    // 加载驱动程序
    auto tarfs        = util::owner(new tarfs::TarFSDriver());
    auto register_res = vfs.register_fs(tarfs);
    propagate(register_res);

    auto initrd_device = make_initrd();
    auto mount_res     = vfs.mount("tarfs", initrd_device, INITRD_PATH,
                                   MountFlags::NONE, nullptr);
    propagate(mount_res);
    void_return();
}

Result<void> init_scheduler() {
    auto kernel_res = task::TaskManager::inst().create_kernel_task();
    if (!kernel_res.has_value()) {
        loggers::SUSTCORE::ERROR("创建KERNEL进程失败! 错误码: %s",
                                 to_cstring(kernel_res.error()));
        propagate_return(kernel_res);
    }

    auto idle_res = task::TaskManager::inst().create_idle_thread();
    if (!idle_res.has_value()) {
        loggers::SUSTCORE::ERROR("创建idle内核线程失败! 错误码: %s",
                                 to_cstring(idle_res.error()));
        propagate_return(idle_res);
    }

    auto load_res = task::TaskManager::inst().load_init(INITMOD_PATH);
    if (!load_res.has_value()) {
        loggers::SUSTCORE::ERROR("加载初始进程失败! 错误码: %s",
                                 to_cstring(load_res.error()));
        propagate_return(load_res);
    }

    auto task = load_res.value();
    assert(task->threads.size() == 1);
    schd::Scheduler::init(idle_res.value(), task->threads.front());
    schd::Scheduler::inst().init();
#ifdef __CONF_KERNEL_TESTS
    auto kthread_test_res = test::kthread::start_logger_yield_test();
    propagate(kthread_test_res);
#endif
    void_return();
}

void after_init() {
    // 打开中断
    Interrupt::sti();

    // Kernel tests
#ifdef __CONF_KERNEL_TESTS
    TestFramework framework;
    collect_tests(framework);
    framework.run_all();
    loggers::SUSTCORE::INFO("Test complete.");
#endif

#ifdef __CONF_KERNEL_RUN_MODULES
    // Run kernel modules
    schd::Scheduler::inst().run_current();
#endif

    while (true);
}

void init_kop();

extern "C" void post_init(void) {
    loggers::SUSTCORE::INFO("已进入 post-init 阶段");
    auto &e = env::inst();

    // 将 pre-init 阶段中初始化的子系统再次初始化, 以适应内核虚拟地址空间
    GFP::post_init();
    PageMan::init();

    Allocator::init();

    // 初始化中断处理程序
    Interrupt::init();
    // TODO: 按理来说这个东西应该放在下面的
    // 但是我忘记把device tree给提升到kernel physical address space中了
    // 我现在也改不动了
    // 就先放在这吧
    Initialization::post_init();

    // 将低端内存设置为用户态
    auto &meminfo = e.meminfo();
    PageMan kernelman(kernel_root);
    kernelman.modify_range_flags<PageMan::ModifyMask::U>(
        meminfo.lowvm, meminfo.uppm - meminfo.lowpm, PageMan::RWX::NONE, true,
        false);

    // 初始化 kernel object pool
    loggers::SUSTCORE::INFO("初始化内核对象池");
    init_kop();

    loggers::SUSTCORE::INFO("初始化权限系统");
    cap::CHolderManager::init();

    loggers::SUSTCORE::INFO("初始化VFS");
    auto init_res = init_vfs();
    if (!init_res.has_value()) {
        loggers::SUSTCORE::ERROR("初始化VFS失败! 错误码: %s",
                                 to_cstring(init_res.error()));
        while (true);
    }

    loggers::SUSTCORE::INFO("初始化任务管理器");
    task::TaskManager::init();

    loggers::SUSTCORE::INFO("初始化调度器");
    init_res = init_scheduler();
    if (!init_res.has_value()) {
        loggers::SUSTCORE::ERROR("初始化Scheduler失败! 错误码: %s",
                                 to_cstring(init_res.error()));
        while (true);
    }

    task::wait::WaitReasonManager::init();

    loggers::SUSTCORE::INFO("测试输出ErrCode::BUSY", to_cstring(ErrCode::BUSY));

    after_init();
}

extern "C" void redive(void);

void pre_init() {
    [[maybe_unused]]
    constexpr KernelStage STAGE = KernelStage::PRE_INIT;
    // construct the env
    env::construct();

    Initialization::pre_init();

    auto &e         = env::inst();
    auto detect_res = MemoryLayout::detect();

    if (!detect_res.has_value()) {
        loggers::SUSTCORE::FATAL("探测内存区域失败!错误码: %s",
                                 to_cstring(detect_res.error()));
        while (true);
    }

    PhyAddr upper_bound = PhyAddr::null;
    for (int i = 0; i < e.meminfo().region_cnt; i++) {
        const auto &reg = e.meminfo().regions[i];
        PhyAddr start   = reg.ptr;
        PhyAddr end     = start + reg.size;

        loggers::SUSTCORE::INFO("探测到内存区域 %d: [%p, %p) Status: %d", i,
                                start.addr(), end.addr(),
                                static_cast<int>(reg.status));
        if (upper_bound < end) {
            upper_bound = end;
        }
    }
    e.meminfo(key::main()).uppm = upper_bound;

    loggers::SUSTCORE::INFO("初始化GFP");
    GFP::pre_init();

    loggers::SUSTCORE::INFO("初始化内核地址空间管理器");
    EarlyPageMan::init();

    kernel_paging_setup();

    // FDTHelper::print_device_tree_detailed();

    // 进入 post-init 阶段
    // 此阶段内, 内核的所有代码和数据均已映射到内核虚拟地址空间
    // 将 redive() 函数定位到KvaAddr
    typedef void (*RediveFuncType)(void);
    auto redive_paddr  = (PhyAddr)(void *)redive;
    auto redive_kvaddr = convert<KvaAddr>(redive_paddr);
    loggers::SUSTCORE::DEBUG("redive函数物理地址: %p, 内核虚拟地址: %p",
                             redive_paddr.addr(), redive_kvaddr.addr());
    auto redive_func = (RediveFuncType)redive_kvaddr.addr();
    loggers::SUSTCORE::DEBUG("跳转到内核虚拟地址空间中的redive函数: %p",
                             redive_func);
    redive_func();
    loggers::SUSTCORE::ERROR("redive函数返回了, 这不应该发生!");
    while (true);
}

void kernel_setup(void) {
    pre_init();
    while (true);
}
