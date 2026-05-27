/**
 * @file isr.c
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 中断处理例程
 * @version alpha-1.0.0
 * @date 2025-11-19
 *
 * @copyright Copyright (c) 2025
 *
 */

#include <arch/riscv64/description.h>
#include <arch/riscv64/device/misc.h>
#include <arch/riscv64/int/isr.h>
#include <arch/riscv64/trait.h>
#include <env.h>
#include <logger.h>
#include <mem/kaddr.h>
#include <sbi/sbi.h>
#include <sus/logger.h>
#include <sustcore/addr.h>
#include <sustcore/epacks.h>
#include <syscall/syscall.h>
#include <task/scheduler.h>
#include <task/task_struct.h>

namespace Exceptions {
    constexpr umb_t INST_MISALIGNED    = 0;   // 指令地址不对齐
    constexpr umb_t INST_ACCESS_FAULT  = 1;   // 指令访问错误
    constexpr umb_t ILLEGAL_INST       = 2;   // 非法指令
    constexpr umb_t BREAKPOINT         = 3;   // 断点
    constexpr umb_t LOAD_MISALIGNED    = 4;   // 加载地址
    constexpr umb_t LOAD_ACCESS_FAULT  = 5;   // 加载访问错误
    constexpr umb_t STORE_MISALIGNED   = 6;   // 存储
    constexpr umb_t STORE_ACCESS_FAULT = 7;   // 存储访问错误
    constexpr umb_t ECALL_U            = 8;   // 用户模式环境调用
    constexpr umb_t ECALL_S            = 9;   // 监管模式环境调用
    constexpr umb_t RESERVED_0         = 10;  // 保留
    constexpr umb_t RESERVED_1         = 11;  // 保留
    constexpr umb_t INST_PAGE_FAULT    = 12;  // 取指页
    constexpr umb_t LOAD_PAGE_FAULT    = 13;  // 加载页错误
    constexpr umb_t RESERVED_2         = 14;  // 保留
    constexpr umb_t STORE_PAGE_FAULT   = 15;  // 存储页
    constexpr umb_t RESERVED_3         = 16;  // 保留
    constexpr umb_t RESERVED_4         = 17;  // 保留
    constexpr umb_t SOFTWARE_CHECK     = 18;  // 软件检查异常
    constexpr umb_t HARDWARE_EEROR     = 19;  // 硬件错误

    const char *MSG[] = {"指令地址不对齐",
                         "指令访问错误",
                         "非法指令",
                         "断点",
                         "加载地址不对齐",
                         "加载访问错误",
                         "存储地址不对齐",
                         "存储访问错误",
                         "用户模式环境调用",
                         "监管模式环境调用",
                         "保留",
                         "保留",
                         "取指页错误",
                         "加载页错误",
                         "保留",
                         "存储页错误",
                         "保留",
                         "保留",
                         "软件检查异常",
                         "硬件错误"};
};  // namespace Exceptions

namespace {
    const char *exception_name(umb_t cause) {
        if (cause < sizeof(Exceptions::MSG) / sizeof(Exceptions::MSG[0])) {
            return Exceptions::MSG[cause];
        }
        return "未知";
    }

    const char *privilege_name(const Riscv64Context *ctx) {
        if (ctx == nullptr) {
            return "未知";
        }
        return ctx->sstatus.spp ? "S-Mode" : "U-Mode";
    }

    const char *page_fault_kind(umb_t cause) {
        switch (cause) {
            case Exceptions::INST_PAGE_FAULT:  return "instruction";
            case Exceptions::LOAD_PAGE_FAULT:  return "load";
            case Exceptions::STORE_PAGE_FAULT: return "store";
            default:                           return "not-page-fault";
        }
    }

    const char *page_size_name(PageMan::PageSize size) {
        switch (size) {
            case PageMan::PageSize::_4K:   return "4K";
            case PageMan::PageSize::_2M:   return "2M";
            case PageMan::PageSize::_1G:   return "1G";
            case PageMan::PageSize::_NULL:
            default:                       return "null";
        }
    }

    void log_current_task_error() {
        if (!schd::Scheduler::initialized()) {
            loggers::INTERRUPT::ERROR("task: scheduler not initialized");
            return;
        }

        auto *tcb = schd::Scheduler::inst().current_tcb();
        if (tcb == nullptr) {
            loggers::INTERRUPT::ERROR("task: none");
            return;
        }
        loggers::INTERRUPT::ERROR(
            "task: pid=%lu, tid=%lu, state=%d, kstack_top=%p",
            tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid,
            static_cast<int>(tcb->basic_entity.state), tcb->kstack_top);
    }

    void log_trap_context_error(csr_scause_t scause, umb_t sepc, umb_t stval,
                                const Riscv64Context *ctx) {
        loggers::INTERRUPT::ERROR(
            "trap: cause=%s(%lu), scause=0x%lx, sepc=0x%lx, stval=0x%lx",
            exception_name(scause.cause), scause.cause, scause.value, sepc,
            stval);
        if (ctx == nullptr) {
            loggers::INTERRUPT::ERROR("ctx: null");
            return;
        }
        loggers::INTERRUPT::ERROR(
            "ctx: ptr=%p, mode=%s, sstatus=0x%lx, sp=0x%lx, ra=0x%lx", ctx,
            privilege_name(ctx), ctx->sstatus.value,
            ctx->regs[Context::X1_BASE + 1], ctx->regs[Context::RA_BASE]);
        loggers::INTERRUPT::ERROR(
            "args: a0=0x%lx, a1=0x%lx, a2=0x%lx, a3=0x%lx",
            ctx->regs[Context::A0_BASE], ctx->regs[Context::A0_BASE + 1],
            ctx->regs[Context::A0_BASE + 2], ctx->regs[Context::A0_BASE + 3]);
        loggers::INTERRUPT::ERROR(
            "args: a4=0x%lx, a5=0x%lx, a6=0x%lx, a7=0x%lx",
            ctx->regs[Context::A0_BASE + 4], ctx->regs[Context::A0_BASE + 5],
            ctx->regs[Context::A0_BASE + 6], ctx->regs[Context::A0_BASE + 7]);
    }

    void log_pte_debug(VirAddr addr, PageMan &pman) {
        auto query_res = pman.query_page(addr);
        if (!query_res.has_value()) {
            loggers::INTERRUPT::DEBUG(
                "pte: addr=%p, query_err=%s(%d)", addr.addr(),
                to_cstring(query_res.error()), query_res.error());
            return;
        }
        auto qres        = query_res.value();
        PageMan::PTE pte = *qres.pte;
        loggers::INTERRUPT::DEBUG(
            "pte: addr=%p, value=0x%lx, pa=%p, size=%s, rwx=0x%lx", addr.addr(),
            pte.value, PageMan::get_physical_address(pte).addr(),
            page_size_name(qres.size), pte.rwx);
        loggers::INTERRUPT::DEBUG(
            "pte flags: V=%d U=%d G=%d A=%d D=%d NP=%d COW=%d", pte.v, pte.u,
            pte.g, pte.a, pte.d, pte.np, PageMan::is_cow(pte));
    }

    void log_pte_error(VirAddr addr, PageMan &pman) {
        auto query_res = pman.query_page(addr);
        if (!query_res.has_value()) {
            loggers::INTERRUPT::ERROR(
                "pte: addr=%p, query_err=%s(%d)", addr.addr(),
                to_cstring(query_res.error()), query_res.error());
            return;
        }
        auto qres        = query_res.value();
        PageMan::PTE pte = *qres.pte;
        loggers::INTERRUPT::ERROR(
            "pte: addr=%p, value=0x%lx, pa=%p, size=%s, rwx=0x%lx", addr.addr(),
            pte.value, PageMan::get_physical_address(pte).addr(),
            page_size_name(qres.size), pte.rwx);
        loggers::INTERRUPT::ERROR(
            "pte flags: V=%d U=%d G=%d A=%d D=%d NP=%d COW=%d", pte.v, pte.u,
            pte.g, pte.a, pte.d, pte.np, PageMan::is_cow(pte));
    }
}  // namespace

namespace handlers::paging {
    enum class FaultCause {
        NO_PRESENT,       // No present page
        SAU_NO_SUM,       // S-mode Access User page without SUM
        SEU,              // S-mode Execute User page
        UAS,              // User Access Supervisor page
        INVALID_AD,       // Invalid A/D bit
        WRITE_PROTECT,    // Write protection
        EXECUTE_PROTECT,  // Execute protection
        UNKNOWN           // Unknown cause
    };

    static const char *fault_cause_name(FaultCause cause) {
        switch (cause) {
            case FaultCause::NO_PRESENT:      return "NO_PRESENT";
            case FaultCause::SAU_NO_SUM:      return "SAU_NO_SUM";
            case FaultCause::SEU:             return "SEU";
            case FaultCause::UAS:             return "UAS";
            case FaultCause::INVALID_AD:      return "INVALID_AD";
            case FaultCause::WRITE_PROTECT:   return "WRITE_PROTECT";
            case FaultCause::EXECUTE_PROTECT: return "EXECUTE_PROTECT";
            case FaultCause::UNKNOWN:
            default:                          return "UNKNOWN";
        }
    }

    static void log_page_fault_error(csr_scause_t scause, umb_t sepc,
                                     umb_t stval, const Riscv64Context *ctx,
                                     FaultCause cause, PageMan &pman) {
        VirAddr fault_addr = VirAddr(stval);
        loggers::INTERRUPT::ERROR(
            "page fault: kind=%s, fault_cause=%s, addr=%p, page=%p",
            page_fault_kind(scause.cause), fault_cause_name(cause),
            fault_addr.addr(), fault_addr.page_align_down().addr());
        log_trap_context_error(scause, sepc, stval, ctx);
        log_current_task_error();
        log_pte_error(fault_addr, pman);
    }

    static FaultCause confirm_fault_cause(const csr_scause_t &scause,
                                          const VirAddr &fault_addr,
                                          const Riscv64Context *ctx,
                                          PageMan &pman) {
        if (scause.cause != Exceptions::INST_PAGE_FAULT &&
            scause.cause != Exceptions::LOAD_PAGE_FAULT &&
            scause.cause != Exceptions::STORE_PAGE_FAULT)
        {
            return FaultCause::UNKNOWN;
        }

        auto query_res = pman.query_page(fault_addr);
        if (!query_res.has_value()) {
            // 页面不存在
            if (query_res.error() == ErrCode::PAGE_NOT_PRESENT) {
                return FaultCause::NO_PRESENT;
            }
            loggers::INTERRUPT::ERROR("查询页表时发生错误: addr=%p, err=%d",
                                      fault_addr.addr(), query_res.error());
            return FaultCause::UNKNOWN;
        }

        auto qres                  = query_res.value();
        typename PageMan::PTE *pte = qres.pte;

        // 页面不存在
        if (!PageMan::is_present(*pte)) {
            return FaultCause::NO_PRESENT;
        }

        int acc_priv =
            ctx->sstatus.spp ? 1 : 0;  // 访问发生时的特权级, 0=用户态, 1=内核态
        int pte_priv = pte->u ? 0 : 1;  // 页表项的特权级, 0=用户页, 1=内核页
        int priv_gap =
            acc_priv - pte_priv;  // 访问特权级与页表项特权级的差值
                                  // = 0 说明访问特权级与页表项特权级相同
                                  // > 0 说明访问特权级比页表项特权级高
                                  // < 0 说明访问特权级比页表项特权级低

        // 访问特权级比页表项特权级高, 说明是内核态访问用户页
        if (priv_gap > 0) {
            if (scause.cause == Exceptions::INST_PAGE_FAULT) {
                return FaultCause::SEU;
            } else if (!ctx->sstatus.sum) {
                return FaultCause::SAU_NO_SUM;
            }
        }

        // PTE为S页, 但访问发生在U态
        if (priv_gap < 0) {
            return FaultCause::UAS;
        }

        PageMan::RWX rwx = PageMan::rwx(*pte);
        // 存储页异常且页面不可写
        if ((scause.cause == Exceptions::STORE_PAGE_FAULT) &&
            !PageMan::is_writable(rwx))
        {
            return FaultCause::WRITE_PROTECT;
        }
        // 取指页异常且页面不可执行
        if ((scause.cause == Exceptions::INST_PAGE_FAULT) &&
            !PageMan::is_executable(rwx))
        {
            return FaultCause::EXECUTE_PROTECT;
        }
        // 取指页异常且是高特权级访问用户页
        if ((scause.cause == Exceptions::INST_PAGE_FAULT) &&
            !PageMan::is_executable(rwx))
        {
            return FaultCause::EXECUTE_PROTECT;
        }

        // AD位错误
        if (!pte->a) {
            return FaultCause::INVALID_AD;
        }
        // 存储页异常且D位未设置但页面可写
        if ((scause.cause == Exceptions::STORE_PAGE_FAULT) && !pte->d &&
            PageMan::is_writable(rwx))
        {
            return FaultCause::INVALID_AD;
        }

        return FaultCause::UNKNOWN;
    }

    bool paging_fault(csr_scause_t scause, umb_t sepc, umb_t stval,
                      Riscv64Context *ctx) {
        const VirAddr fault_addr = VirAddr(stval);
        const VirAddr fault_page = fault_addr.page_align_down();
        auto &e                  = env::inst();
        loggers::INTERRUPT::DEBUG(
            "page fault: kind=%s, addr=%p, page=%p, sepc=0x%lx",
            page_fault_kind(scause.cause), fault_addr.addr(), fault_page.addr(),
            sepc);
        loggers::INTERRUPT::DEBUG("page fault env: pgd=%p, tm=%p",
                                  e.pgd().addr(), e.tmm());
        PageMan pman(e.pgd());

        if (!e.pgd().nonnull()) {
            loggers::INTERRUPT::ERROR("page fault: 当前页表根为空");
            log_trap_context_error(scause, sepc, stval, ctx);
            log_current_task_error();
            return false;
        }

        FaultCause cause = confirm_fault_cause(scause, fault_addr, ctx, pman);

        bool processed = false;

        // 在此处可以允许中断发生

        switch (cause) {
            case FaultCause::NO_PRESENT: {
                // 使用缺页异常处理程序处理缺页异常
                auto *tm = e.tmm();
                if (tm != nullptr) {
                    loggers::INTERRUPT::DEBUG(
                        "缺页异常可尝试处理: addr=%p, page=%p, tm_pgd=%p",
                        fault_addr.addr(), fault_page.addr(), tm->pgd().addr());
                    processed |= tm->on_np({fault_addr});

                    // for debug:
                    if (processed) {
                        // 再次查询, 验证该页已被映射
                        PhyAddr hw_root_after = PageMan::read_root();
                        PageMan verify_pman(hw_root_after);
                        auto verify_res = verify_pman.query_page(fault_addr);
                        if (!verify_res.has_value()) {
                            loggers::INTERRUPT::ERROR(
                                "TM::on_np 返回成功但页面仍不存在: addr=%p, "
                                "err=%d, hw_root_after=%p",
                                fault_addr.addr(), verify_res.error(),
                                hw_root_after.addr());
                        } else {
                            loggers::INTERRUPT::DEBUG(
                                "缺页异常已处理: addr=%p, page=%p, "
                                "hw_root_after=%p",
                                fault_addr.addr(), fault_page.addr(),
                                hw_root_after.addr());
                            log_pte_debug(fault_addr, verify_pman);
                        }
                    }
                }
                break;
            }
            case FaultCause::SAU_NO_SUM: {
                break;
            }
            case FaultCause::UAS: {
                break;
            }
            case FaultCause::SEU: {
                break;
            }
            case FaultCause::INVALID_AD: {
                auto query_res = pman.query_page(fault_addr);
                if (!query_res.has_value()) {
                    loggers::INTERRUPT::ERROR(
                        "处理 A/D 位错误时查询页表失败: addr=%p, err=%d",
                        fault_addr.addr(), query_res.error());
                    break;
                }
                PageMan::PTE *pte = query_res.value().pte;
                bool present      = PageMan::is_present(*pte);
                if (!present) {
                    loggers::INTERRUPT::ERROR(
                        "处理 A/D 位错误时页面不存在: addr=%p, pte=%p",
                        fault_addr.addr(), pte);
                    break;
                }

                bool updated = false;

                if (!pte->a) {
                    cause = FaultCause::INVALID_AD;
                }

                PageMan::RWX rwx = PageMan::rwx(*pte);
                if ((scause.cause == Exceptions::STORE_PAGE_FAULT) && !pte->d &&
                    PageMan::is_writable(rwx))
                {
                    cause = FaultCause::INVALID_AD;
                }

                processed |= updated;
                if (updated) {
                    PageMan::flush_tlb();
                    loggers::INTERRUPT::DEBUG(
                        "修复 A/D 位后重试: addr=%p, A=%d, D=%d",
                        fault_addr.addr(), pte->a, pte->d);
                }
                break;
            }
            case FaultCause::WRITE_PROTECT: {
                auto *tm = e.tmm();
                if (tm != nullptr) {
                    processed |= tm->on_wp(fault_addr);
                }
                if (processed) {
                    loggers::INTERRUPT::DEBUG(
                        "写保护页异常已处理: addr=%p, page=%p",
                        fault_addr.addr(), fault_page.addr());
                    log_pte_debug(fault_addr, pman);
                }
                break;
            }
            case FaultCause::EXECUTE_PROTECT: {
                break;
            }
            default: {
                break;
            }
        }

        if (!processed) {
            log_page_fault_error(scause, sepc, stval, ctx, cause, pman);
        }

        return processed;

        // 接下来应该执行页异常相关处理
        // 1. 检查地址是否合法
        // 2. 如果是缺页, 则分配物理页并映射
        // 2.1 如果是缺页, 且该页属于虚拟内存, 则从磁盘中调入
        // 2.2 如果是写保护错误, 则检查是否为写时复制
        // 2.3 更新页表项和TLB
        // 3. 如果是权限错误, 则终止相关进程
    }
}  // namespace handlers::paging

namespace Handlers {

    bool illegal_instruction(csr_scause_t scause, umb_t sepc, umb_t stval,
                             Riscv64Context *ctx) {
        loggers::INTERRUPT::DEBUG(
            "进入非法指令异常处理程序: sepc=0x%016lx, stval=0x%016lx", sepc,
            stval);
        return false;  // 无法处理该异常
    }

    void exception(csr_scause_t scause, umb_t sepc, umb_t stval,
                   Riscv64Context *ctx) {
        bool processed = false;
        switch (scause.cause) {
            case Exceptions::ECALL_U: {
                env::inst().trap_context(env::key::trap_context()) = ctx;
                auto *current_tcb = schd::Scheduler::inst().current_tcb();
                assert(current_tcb != nullptr);
                syscall::ArgPack args = ctx->read_args();
                loggers::INTERRUPT::DEBUG(
                    "系统调用触发: name=%s, no=0x%lx, pid=%lu, tid=%lu",
                    syscall::name_of(args.syscall_number), args.syscall_number,
                    current_tcb->task != nullptr ? current_tcb->task->pid : 0,
                    current_tcb->tid);
                loggers::INTERRUPT::DEBUG(
                    "系统调用参数: capidx=0x%lx, arg0=0x%lx, arg1=0x%lx, "
                    "arg2=0x%lx",
                    args.capidx, args.args[0], args.args[1], args.args[2]);
                loggers::INTERRUPT::DEBUG(
                    "系统调用参数: arg3=0x%lx, arg4=0x%lx, sepc=0x%lx",
                    args.args[3], args.args[4], sepc);
                current_tcb->syscall_info.begin(args);
                current_tcb->syscall_info.context = task::SyscallContext{
                    .tcb          = current_tcb,
                    .pcb          = current_tcb->task,
                    .tmm          = current_tcb->task != nullptr
                                        ? current_tcb->task->tmm.get()
                                        : nullptr,
                    .trap_context = ctx,
                };
                ctx->sepc += 4;
                if (!syscall::is_suspendable_syscall(args.syscall_number)) {
                    auto ret = syscall::dispatch_sync(*current_tcb);
                    current_tcb->syscall_info.complete(ret);
                    loggers::INTERRUPT::DEBUG(
                        "同步 syscall 立即完成: pid=%lu tid=%lu sysno=0x%lx",
                        current_tcb->task != nullptr ? current_tcb->task->pid : 0,
                        current_tcb->tid, args.syscall_number);
                } else {
                    auto task = syscall::dispatch_async(*current_tcb);
                    if (current_tcb->syscall_info.completed()) {
                        loggers::INTERRUPT::DEBUG(
                            "异步 syscall 立即完成, 不进入协程队列: pid=%lu tid=%lu",
                            current_tcb->task != nullptr ? current_tcb->task->pid : 0,
                            current_tcb->tid);
                    } else if (current_tcb->basic_entity.state ==
                           ThreadState::WAITING)
                    {
                        loggers::INTERRUPT::DEBUG(
                            "syscall 已进入等待, 由等待系统负责恢复: pid=%lu tid=%lu sysno=0x%lx",
                            current_tcb->task != nullptr ? current_tcb->task->pid : 0,
                            current_tcb->tid,
                            current_tcb->syscall_info.syscall_number);
                        task.detach();
                    } else {
                        if (current_tcb->syscall_info.handle == nullptr) {
                            current_tcb->syscall_info.handle = task.handle();
                        }
                        current_tcb->basic_entity
                            .template flags_set<schd::SchedMeta::FLAGS_NEED_RESCHED>();
                        loggers::INTERRUPT::INFO(
                            "syscall 协程延后到调度前继续执行: pid=%lu tid=%lu sysno=0x%lx",
                            current_tcb->task != nullptr ? current_tcb->task->pid : 0,
                            current_tcb->tid,
                            current_tcb->syscall_info.syscall_number);
                        task.detach();
                    }
                }
                processed = true;
                env::inst().trap_context(env::key::trap_context()) = nullptr;
                break;
            }
            case Exceptions::ILLEGAL_INST:
                processed = illegal_instruction(scause, sepc, stval, ctx);
                break;
            case Exceptions::INST_PAGE_FAULT:
            case Exceptions::LOAD_PAGE_FAULT:
            case Exceptions::STORE_PAGE_FAULT:
                processed =
                    handlers::paging::paging_fault(scause, sepc, stval, ctx);
                break;
            default:
                loggers::INTERRUPT::INFO("无异常处理程序!");
                processed = false;
                break;
        }

        if (!processed) {
            loggers::INTERRUPT::ERROR("发生异常: %s (%lu), mode=%s",
                                      exception_name(scause.cause),
                                      scause.cause, privilege_name(ctx));
            loggers::INTERRUPT::ERROR("无法处理该异常, 需终止相关进程");
            if (scause.cause != Exceptions::INST_PAGE_FAULT &&
                scause.cause != Exceptions::LOAD_PAGE_FAULT &&
                scause.cause != Exceptions::STORE_PAGE_FAULT)
            {
                log_trap_context_error(scause, sepc, stval, ctx);
                log_current_task_error();
            }
            while (true);
        }
    }

    void timer(csr_scause_t scause, umb_t sepc, umb_t stval,
               Riscv64Context *ctx) {
        // 计算时间差
        units::tick current_ticks = csr_get_time();
        units::tick gap_ticks     = current_ticks - timer_info.last_ticks;

        TimerTickEvent e = {.last_tick = timer_info.last_ticks,
                            .increment = timer_info.increment,
                            .gap_ticks = gap_ticks};

        timer_info.last_ticks = current_ticks;
        schd::Scheduler::inst().do_tick(e);

        // 重新设置下一次时钟中断
        sbi_legacy_set_timer(current_ticks + timer_info.increment);
    }
}  // namespace Handlers
