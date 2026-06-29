/**
 * @file task.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 进程/线程基础设施
 * @version alpha-1.0.0
 * @date 2026-01-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <driver/clock.h>
#include <env.h>
#include <logger.h>
#include <mem/alloc.h>
#include <mem/slub.h>
#include <object/task.h>
#include <storage.h>
#include <sus/raii.h>
#include <task/task.h>
#include <task/wait.h>
#include <vfs/procfs.h>

#include <cassert>
#include <algorithm>

namespace task {
    namespace {
        wait::wd_t g_task_exit_wait_wd = 0;
    }  // namespace

    struct NanosleepContext {
        wait::wd_t wait_wd = 0;
        // 这是为避免 “timer 先触发、线程后入队” 的丢唤醒临时措施。
        // 当前实现依赖该状态位与 wait_event 条件配合工作，后续应考虑
        // 以更完整的同步与生命周期模型替代。
        bool fired = false;
        bool sleeping = false;
        bool armed = false;
        device::ExpireHandle timer_handle{};

        NanosleepContext() noexcept : wait_wd(wait::alloc_reason()) {}
    };

    struct TimedWaitContext {
        wait::wd_t wait_wd = 0;
        bool timed_out = false;
        bool waiting = false;
        bool armed = false;
        device::ExpireHandle timer_handle{};
    };

    namespace {
        constexpr size_t TIMEOUT_CONTEXT_NANOSLEEP = 1;
        constexpr size_t TIMEOUT_CONTEXT_TIMEDWAIT = 2;

        [[nodiscard]]
        constexpr size_t encode_timeout_context(void *ctx,
                                                size_t type) noexcept {
            return (reinterpret_cast<size_t>(ctx) & ~static_cast<size_t>(0x3)) |
                   type;
        }

        [[nodiscard]]
        constexpr size_t decode_timeout_context_type(size_t value) noexcept {
            return value & static_cast<size_t>(0x3);
        }

        [[nodiscard]]
        constexpr void *decode_timeout_context_ptr(size_t value) noexcept {
            return reinterpret_cast<void *>(value & ~static_cast<size_t>(0x3));
        }
    }  // namespace

    wait::wd_t task_exit_wait_wd() noexcept {
        if (g_task_exit_wait_wd == 0 && wait::WaitReasonManager::initialized()) {
            g_task_exit_wait_wd = wait::alloc_reason();
        }
        return g_task_exit_wait_wd;
    }

    Result<NanosleepContext *> ensure_nanosleep_context(
        util::nonnull<TCB *> tcb) noexcept {
        if (tcb->nanosleep_ctx != nullptr) {
            return tcb->nanosleep_ctx;
        }

        auto *ctx = new NanosleepContext();
        if (ctx == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        tcb->nanosleep_ctx = ctx;
        return ctx;
    }

    void destroy_nanosleep_context(TCB *tcb) noexcept {
        if (tcb == nullptr || tcb->nanosleep_ctx == nullptr) {
            return;
        }
        auto *ctx = tcb->nanosleep_ctx;
        auto *time_keeper =
            env::hart_ctx != nullptr ? env::hart_ctx->time_keeper() : nullptr;
        if (ctx->armed && time_keeper != nullptr) {
            (void)time_keeper->cancel(ctx->timer_handle);
        }
        delete tcb->nanosleep_ctx;
        tcb->nanosleep_ctx = nullptr;
    }

    Result<TimedWaitContext *> ensure_timed_wait_context(
        util::nonnull<TCB *> tcb) noexcept {
        if (tcb->timed_wait_ctx != nullptr) {
            return tcb->timed_wait_ctx;
        }

        auto *ctx = new TimedWaitContext();
        if (ctx == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        tcb->timed_wait_ctx = ctx;
        return ctx;
    }

    void destroy_timed_wait_context(TCB *tcb) noexcept {
        if (tcb == nullptr || tcb->timed_wait_ctx == nullptr) {
            return;
        }
        auto *ctx = tcb->timed_wait_ctx;
        auto *time_keeper =
            env::hart_ctx != nullptr ? env::hart_ctx->time_keeper() : nullptr;
        if (ctx->armed && time_keeper != nullptr) {
            (void)time_keeper->cancel(ctx->timer_handle);
        }
        delete tcb->timed_wait_ctx;
        tcb->timed_wait_ctx = nullptr;
    }

    Result<void> arm_timed_wait(util::nonnull<TCB *> tcb, wait::wd_t wait_wd,
                                size_t timeout_ns) noexcept {
        auto ctx_res = ensure_timed_wait_context(tcb);
        propagate(ctx_res);
        auto *ctx = ctx_res.value();
        if (ctx == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        auto *time_keeper =
            env::hart_ctx != nullptr ? env::hart_ctx->time_keeper() : nullptr;
        if (time_keeper == nullptr || time_keeper->source() == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        units::time now =
            time_keeper->source()->to_ns(time_keeper->source()->now());
        units::time deadline =
            now + units::time::from_nanoseconds(static_cast<int64_t>(timeout_ns));

        ctx->wait_wd   = wait_wd;
        ctx->timed_out = false;
        ctx->waiting   = true;
        ctx->armed     = false;
        ctx->timer_handle.reset();
        tcb->timeout   = false;

        auto enqueue_res = time_keeper->enqueue(device::ExpireAction{
            .expireTime = static_cast<size_t>(deadline.to_nanoseconds()),
            .expireAction = device::expact::TIMEOUT,
            .expireArg0   = tcb->tid,
            .expireArg1   = 0,
        });
        propagate(enqueue_res);
        ctx->timer_handle = enqueue_res.value();
        ctx->armed        = ctx->timer_handle.valid();
        void_return();
    }

    void disarm_timed_wait(TCB *tcb) noexcept {
        if (tcb == nullptr || tcb->timed_wait_ctx == nullptr) {
            return;
        }
        auto *ctx = tcb->timed_wait_ctx;
        auto *time_keeper =
            env::hart_ctx != nullptr ? env::hart_ctx->time_keeper() : nullptr;
        if (ctx->armed && time_keeper != nullptr) {
            (void)time_keeper->cancel(ctx->timer_handle);
        }
        ctx->armed     = false;
        ctx->timer_handle.reset();
        ctx->waiting   = false;
        ctx->timed_out = false;
        ctx->wait_wd   = 0;
    }

    bool timed_wait_timed_out(const TCB *tcb) noexcept {
        if (tcb == nullptr || tcb->timed_wait_ctx == nullptr) {
            return false;
        }
        return tcb->timed_wait_ctx->timed_out;
    }

    TCB *lookup_tcb_by_tid(tid_t tid) noexcept {
        if (!TaskManager::initialized()) {
            return nullptr;
        }

        auto pids = TaskManager::inst().snapshot_pids();
        for (auto pid : pids) {
            auto pcb_res = TaskManager::inst().lookup_pcb_by_pid(pid);
            if (!pcb_res.has_value()) {
                continue;
            }
            auto *pcb = pcb_res.value();
            if (pcb == nullptr) {
                continue;
            }
            for (auto &tcb : pcb->threads) {
                if (tcb.tid == tid) {
                    return &tcb;
                }
            }
        }
        return nullptr;
    }

    void mark_tcb_timeout(TCB &tcb) noexcept {
        tcb.timeout = true;
        if (tcb.timed_wait_ctx != nullptr) {
            tcb.timed_wait_ctx->timed_out = true;
            tcb.timed_wait_ctx->waiting   = false;
            tcb.timed_wait_ctx->armed     = false;
            tcb.timed_wait_ctx->timer_handle.reset();
        }
    }

    bool consume_tcb_timeout(TCB &tcb) noexcept {
        if (!tcb.timeout) {
            return false;
        }
        tcb.timeout = false;
        if (tcb.timed_wait_ctx != nullptr) {
            tcb.timed_wait_ctx->timed_out = false;
        }
        return true;
    }

    void process_timeout_tcb(tid_t tid) noexcept {
        auto *tcb = lookup_tcb_by_tid(tid);
        if (tcb == nullptr) {
            loggers::TASK::ERROR("TIMEOUT 无法定位 TCB: tid=%lu",
                                 static_cast<unsigned long>(tid));
            return;
        }

        mark_tcb_timeout(*tcb);
        if (!schd::Scheduler::inst().wakeup_waiting(tcb)) {
            loggers::TASK::ERROR("TIMEOUT 唤醒线程失败: tid=%lu",
                                 static_cast<unsigned long>(tid));
        }
    }

    void process_timeout_wakeup(wait::wd_t wait_wd, size_t context) noexcept {
        switch (decode_timeout_context_type(context)) {
            case TIMEOUT_CONTEXT_NANOSLEEP: {
                auto *ctx = reinterpret_cast<NanosleepContext *>(
                    decode_timeout_context_ptr(context));
                if (ctx != nullptr) {
                    ctx->fired    = true;
                    ctx->sleeping = false;
                    ctx->armed    = false;
                    ctx->timer_handle.reset();
                }
                break;
            }
            case TIMEOUT_CONTEXT_TIMEDWAIT: {
                auto *ctx = reinterpret_cast<TimedWaitContext *>(
                    decode_timeout_context_ptr(context));
                if (ctx != nullptr) {
                    ctx->timed_out = true;
                    ctx->waiting   = false;
                    ctx->armed     = false;
                    ctx->timer_handle.reset();
                }
                break;
            }
            default:
                loggers::TASK::ERROR("未知的超时上下文类型: %lu",
                                     static_cast<unsigned long>(context));
                break;
        }

        auto wake_res = wait::wake_one(wait_wd);
        if (!wake_res.has_value()) {
            loggers::TASK::ERROR("TimeKeeper 超时唤醒失败: wd=%lu err=%s",
                                 static_cast<unsigned long>(wait_wd),
                                 to_cstring(wake_res.error()));
        }
    }

    Result<void> block_current_for_nanosleep(util::nonnull<TCB *> tcb,
                                             size_t ns) noexcept {
        if (ns == 0) {
            void_return();
        }

        auto ctx_res = ensure_nanosleep_context(tcb);
        propagate(ctx_res);
        auto *ctx = ctx_res.value();
        if (ctx == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (ctx->sleeping) {
            unexpect_return(ErrCode::FAILURE);
        }

        auto *time_keeper =
            env::hart_ctx != nullptr ? env::hart_ctx->time_keeper() : nullptr;
        if (time_keeper == nullptr || time_keeper->source() == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        units::time now =
            time_keeper->source()->to_ns(time_keeper->source()->now());
        units::time deadline =
            now + units::time::from_nanoseconds(static_cast<int64_t>(ns));

        ctx->fired    = false;
        ctx->sleeping = true;
        ctx->armed    = false;
        ctx->timer_handle.reset();

        auto enqueue_res = time_keeper->enqueue(device::ExpireAction{
            .expireTime = static_cast<size_t>(deadline.to_nanoseconds()),
            .expireAction = device::expact::WAKEUP,
            .expireArg0   = ctx->wait_wd,
            .expireArg1   = encode_timeout_context(
                ctx, TIMEOUT_CONTEXT_NANOSLEEP),
        });
        propagate(enqueue_res);
        ctx->timer_handle = enqueue_res.value();
        ctx->armed        = ctx->timer_handle.valid();

        auto wait_res = wait_event(ctx->wait_wd, ctx->fired);
        if (!wait_res.has_value()) {
            if (ctx->armed) {
                (void)time_keeper->cancel(ctx->timer_handle);
                ctx->armed = false;
                ctx->timer_handle.reset();
            }
            ctx->sleeping = false;
            propagate_return(wait_res);
        }

        if (ctx->armed) {
            (void)time_keeper->cancel(ctx->timer_handle);
            ctx->armed = false;
            ctx->timer_handle.reset();
        }
        ctx->fired    = false;
        ctx->sleeping = false;
        void_return();
    }

    void TCB::SyscallInfo::reset() noexcept {
        syscall_args   = {};
        syscall_number = 0;
        syscall_result = {};
        syscall_state  = State::NONE;
    }

    void TCB::SyscallInfo::begin(const syscall::ArgPack &args) noexcept {
        reset();
        syscall_args   = args;
        syscall_number = args.syscall_number;
        syscall_state  = State::EXECUTING;
    }

    void TCB::SyscallInfo::complete(const syscall::RetPack &ret) noexcept {
        syscall_result = ret;
        syscall_state  = State::COMPLETED;
    }

    namespace {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        TaskManager inst_task_manager;
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        bool inst_task_manager_initialized = false;
    }  // namespace

    /**************************************************************************
     * TaskManager 基础函数
     **************************************************************************/

    void TaskManager::init() {
        new (&inst_task_manager) TaskManager();
        inst_task_manager_initialized = true;
    }

    bool TaskManager::initialized() {
        return inst_task_manager_initialized;
    }

    TaskManager &TaskManager::inst() {
        if (!initialized()) {
            panic("TaskManager 未初始化!");
        }
        return inst_task_manager;
    }

    Result<util::nonnull<PCB *>> TaskManager::kernel_pcb() {
        if (_kernel_pcb == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return util::nnullforce(_kernel_pcb);
    }

    Result<size_t> TaskManager::lookup_holder_id(pid_t pid) {
        return _pid_map.at_nt(pid)
            .transform_error(always(ErrCode::OUT_OF_BOUNDARY))
            .and_then([](auto *pcb_ptr) -> Result<size_t> {
                PCB *pcb = pcb_ptr == nullptr ? nullptr : *pcb_ptr;
                if (pcb == nullptr || pcb->cholder == nullptr) {
                    unexpect_return(ErrCode::INVALID_PARAM);
                }
                return pcb->cholder->id();
            });
    }

    Result<PCB *> TaskManager::lookup_pcb_by_pid(pid_t pid) noexcept {
        auto pcb_res = _pid_map.at_nt(pid);
        if (!pcb_res.has_value()) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }
        return pcb_res.value() == nullptr ? nullptr : *pcb_res.value();
    }

    std::vector<pid_t> TaskManager::snapshot_pids() const {
        std::vector<pid_t> out{};
        out.reserve(_pid_map.size());
        for (const auto &[pid, _] : _pid_map) {
            out.push_back(pid);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    Result<void> TaskManager::kill_pcb_impl(PCB *pcb, TCB *current_tcb,
                                            int exit_code) noexcept {
        if (pcb == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        auto *runtime_tcb = schd::Scheduler::initialized()
                                ? schd::Scheduler::inst().current_tcb()
                                : nullptr;
        bool killing_current =
            current_tcb != nullptr && current_tcb->task == pcb;

        loggers::TASK::DEBUG("开始终止进程: pid=%lu exit_code=%d", pcb->pid,
                             exit_code);
        pcb->exit_code = exit_code;
        if (pcb->exiting) {
            void_return();
        }
        pcb->exiting = true;
        auto wake_res = wait::wake_all(task::task_exit_wait_wd());
        if (!wake_res.has_value()) {
            loggers::TASK::ERROR("唤醒等待退出进程的线程失败: pid=%lu err=%d",
                                 pcb->pid, wake_res.error());
        }
        for (auto &tcb : pcb->threads) {
            if (&tcb != current_tcb &&
                tcb.basic_entity.state == ThreadState::READY &&
                schd::Scheduler::initialized())
            {
                auto dequeue_res =
                    schd::Scheduler::inst().dequeue(util::nnullforce(&tcb));
                if (!dequeue_res.has_value()) {
                    loggers::TASK::ERROR("pcb kill移除线程失败: tid=%d err=%d",
                                         tcb.tid, dequeue_res.error());
                }
            }
            tcb.basic_entity.state = ThreadState::DYING;
        }
        enqueue_recycle(pcb);
        if (killing_current) {
            current_tcb->basic_entity.state = ThreadState::DYING;
            current_tcb->basic_entity
                .template flags_set<schd::SchedMeta::FLAGS_NEED_RESCHED>();
            if (runtime_tcb == current_tcb) {
                schd::Scheduler::inst().schedule();
            }
        }
        void_return();
    }

    [[noreturn]]
    void TaskManager::on_segv(int exit_code) noexcept {
        if (!schd::Scheduler::initialized()) {
            panic("Scheduler 未初始化, 无法处理 segv");
        }
        auto *current_tcb = schd::Scheduler::inst().current_tcb();
        if (current_tcb == nullptr || current_tcb->task == nullptr ||
            current_tcb->is_kernel)
        {
            panic("当前上下文无法按进程处理 segv");
        }

        auto kill_res = kill_pcb_impl(current_tcb->task, current_tcb, exit_code);
        if (!kill_res.has_value()) {
            panic("on_segv 终止进程失败");
        }
        panic("on_segv 调度后意外返回");
    }

    /**************************************************************************
     * TCB / PCB 基础处理
     **************************************************************************/

    Result<void> TaskManager::init_tcb(
        util::nonnull<TCB *> tcb, util::nonnull<PCB *> task /* ... args*/) {
        tcb->tid            = alloc_tid();
        tcb->task           = task;
        tcb->kentry         = nullptr;
        tcb->karg           = nullptr;
        tcb->list_head      = {};
        tcb->boot_role      = BootThreadRole::NONE;
        tcb->wait_wd        = 0;
        tcb->wait_predicate = {};
        tcb->nanosleep_ctx  = nullptr;
        tcb->timed_wait_ctx = nullptr;
        tcb->wait_head      = {};
        tcb->syscall_info.reset();

        Result<PhyAddr> gfp_res = GFP::get_free_page(TCB::KSTACK_PAGES);
        propagate(gfp_res);

        tcb->kstack_phy    = gfp_res.value() + TCB::KSTACK_PAGES * PAGESIZE;
        tcb->kstack_bottom = convert<KpaAddr>(tcb->kstack_phy).addr();
        tcb->ksp           = (char *)tcb->kstack_bottom;
        env::inst().system_memory_info(env::key::set()).kernel_stack_pages +=
            TCB::KSTACK_PAGES;

        auto *ext_ctx = new ExtContext();
        if (ext_ctx == nullptr) {
            GFP::put_page(tcb->kstack_phy - TCB::KSTACK_SIZE, TCB::KSTACK_PAGES);
            tcb->kstack_phy    = PhyAddr::null;
            tcb->kstack_bottom = nullptr;
            tcb->ksp           = nullptr;
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }
        tcb->ext_ctx = util::owner<ExtContext *>(ext_ctx);
        init_ext_context(*tcb->ext_ctx);
        tcb->ext_ctx_live = false;
        void_return();
    }

    Result<void> TaskManager::init_pcb(util::nonnull<PCB *> pcb) {
        pcb->pid            = alloc_pid();
        pcb->is_kernel      = false;
        pcb->exit_code      = 0;
        pcb->threads        = {};
        pcb->exiting        = false;
        pcb->recycle_queued = false;

        pcb->tmm                   = util::owner<TaskMemoryManager *>(nullptr);
        pcb->cholder               = nullptr;
        pcb->entrypoint            = VirAddr(static_cast<addr_t>(0));
        pcb->linuxproc_entrypoint  = VirAddr(static_cast<addr_t>(0));
        pcb->linux_subsystem_entry = VirAddr(static_cast<addr_t>(0));
        pcb->is_linux_process      = false;
        pcb->proc_state            = nullptr;
        pcb->pcb_cap               = cap::null;
        pcb->main_tcb_cap          = cap::null;
        void_return();
    }

    Result<util::nonnull<TCB *>> TaskManager::create_bound_tcb(
        util::nonnull<PCB *> pcb) {
        util::nonnull<TCB *> tcb = alloc_tcb();
        auto tcb_guard = util::Guard([this, tcb]() { (void)recycle_tcb(tcb); });
        auto init_res  = init_tcb(tcb, pcb);
        if (!init_res.has_value()) {
            loggers::SUSTCORE::ERROR("初始化TCB失败! 错误码: %s",
                                     to_cstring(init_res.error()));
            propagate_return(init_res);
        }
        tcb_guard.release();
        return tcb;
    }

    Result<void> TaskManager::recycle_tcb(util::nonnull<TCB *> tcb) {
        loggers::TASK::DEBUG("回收线程 %d (PID: %d)", tcb->tid,
                             tcb->task != nullptr ? tcb->task->pid : -1);
        if (schd::Scheduler::initialized() &&
            schd::Scheduler::inst().current_tcb() == tcb.get())
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        if (tcb->basic_entity.state == ThreadState::READY &&
            tcb->schd_class != schd::ClassType::BOT)
        {
            auto dequeue_res = schd::Scheduler::inst().dequeue(tcb);
            propagate(dequeue_res);
        }
        if ((tcb->basic_entity.state == ThreadState::WAITING ||
             tcb->basic_entity.state == ThreadState::DYING) &&
            wait::WaitReasonManager::initialized())
        {
            auto remove_res = wait::WaitReasonManager::inst().remove(tcb.get());
            if (!remove_res.has_value() &&
                remove_res.error() != ErrCode::INVALID_PARAM &&
                remove_res.error() != ErrCode::OUT_OF_BOUNDARY)
            {
                propagate_return(remove_res);
            }
        }

        PCB *pcb = tcb->task;
        if (pcb != nullptr) {
            pcb->threads.remove(*tcb);
        }

        if (tcb->kstack_phy.nonnull()) {
            GFP::put_page(tcb->kstack_phy - TCB::KSTACK_SIZE,
                          TCB::KSTACK_PAGES);
            auto &info = env::inst().system_memory_info(env::key::set());
            if (info.kernel_stack_pages >= TCB::KSTACK_PAGES) {
                info.kernel_stack_pages -= TCB::KSTACK_PAGES;
            } else {
                info.kernel_stack_pages = 0;
            }
        }
        destroy_nanosleep_context(tcb.get());
        destroy_timed_wait_context(tcb.get());
        delete tcb->ext_ctx;
        tcb->task          = nullptr;
        tcb->kstack_phy    = PhyAddr::null;
        tcb->kstack_bottom = nullptr;
        tcb->ext_ctx_live  = false;
        delete tcb.get();
        void_return();
    }

    Result<void> TaskManager::terminate_pcb(util::nonnull<PCB *> pcb) {
        if (pcb->is_kernel) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        while (!pcb->threads.empty()) {
            TCB *tcb      = &pcb->threads.front();
            tid_t tid     = tcb->tid;
            auto term_res = recycle_tcb(util::nnullforce(tcb));
            if (!term_res.has_value()) {
                loggers::SUSTCORE::ERROR("终止线程 %d 失败! 错误码: %s", tid,
                                         to_cstring(term_res.error()));
                propagate_return(term_res);
            }
        }

        if (pcb->tmm.get() != nullptr) {
            PhyAddr pgd = pcb->tmm->pgd();
            delete pcb->tmm;
            GFP::page_putpage(pgd);
        }
        if (pcb->cholder != nullptr) {
            auto &chman = cap::CHolderManager::inst();
            auto rm_res = chman.remove_holder(pcb->cholder->id());
            propagate(rm_res);
        }
        if (pcb->proc_state != nullptr) {
            (void)procfs::unregister_process(*pcb);
            delete pcb->proc_state;
            pcb->proc_state = nullptr;
        }

        _pid_map.erase(pcb->pid);
        delete pcb.get();
        void_return();
    }

    void TaskManager::enqueue_recycle(PCB *pcb) {
        if (pcb == nullptr || !pcb->exiting || pcb->recycle_queued) {
            return;
        }
        pcb->recycle_queued = true;
        _recycle_pcbs.push_back(pcb);
    }

    void TaskManager::reap_recycled() {
        while (!_recycle_pcbs.empty()) {
            PCB *pcb = _recycle_pcbs.front();
            _recycle_pcbs.pop_front();
            if (pcb == nullptr) {
                continue;
            }
            pcb->recycle_queued = false;
            loggers::TASK::INFO("回收退出进程: pid=%lu", pcb->pid);
            terminate_pcb(util::nnullforce(pcb));
        }
    }

    namespace kop {
        Storage<KOP<PCB>> pcb_raw;
        Storage<KOP<TCB>> tcb_raw;
        Storage<LockedObject<IrqSaveGuardedLock, KOP<PCB>>> pcb_storage;
        Storage<LockedObject<IrqSaveGuardedLock, KOP<TCB>>> tcb_storage;

        [[nodiscard]]
        LockedObject<IrqSaveGuardedLock, KOP<PCB>> &pcb() {
            return pcb_storage.ref();
        }

        [[nodiscard]]
        LockedObject<IrqSaveGuardedLock, KOP<TCB>> &tcb() {
            return tcb_storage.ref();
        }
    }  // namespace kop

    void init_kop() {
        kop::pcb_raw.construct();
        kop::tcb_raw.construct();
        kop::pcb_storage.construct(kop::pcb_raw.get());
        kop::tcb_storage.construct(kop::tcb_raw.get());
    }

    void *PCB::operator new(size_t size) {
        assert(size == sizeof(PCB));
        return kop::pcb().get()->alloc();
    }

    void PCB::operator delete(void *ptr) {
        kop::pcb().get()->free(static_cast<PCB *>(ptr));
    }

    void *TCB::operator new(size_t size) {
        assert(size == sizeof(TCB));
        return kop::tcb().get()->alloc();
    }

    void TCB::operator delete(void *ptr) {
        kop::tcb().get()->free(static_cast<TCB *>(ptr));
    }
}  // namespace task
