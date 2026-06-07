/**
 * @file syscall.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 系统调用
 * @version alpha-1.0.0
 * @date 2026-05-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <logger.h>
#include <sustcore/addr.h>
#include <sustcore/syscall.h>
#include <syscall/cap.h>
#include <syscall/endpoint.h>
#include <syscall/memory.h>
#include <syscall/notif.h>
#include <syscall/syscall.h>
#include <syscall/task.h>
#include <syscall/uaccess.h>
#include <task/scheduler.h>
#include <task/task_struct.h>
#include <task/wait.h>

#include <cassert>

namespace syscall {
    namespace {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
        task::SyscallContext *inst_active_context = nullptr;

        /**
         * @brief 生成布尔型 syscall 返回包.
         *
         * @param ok 操作结果.
         * @return RetPack 返回包.
         */
        [[nodiscard]]
        RetPack bool_ret(bool ok) noexcept {
            return RetPack{.processed = true,
                           .ret0      = static_cast<b64>(ok),
                           .ret1      = static_cast<b64>(ErrCode::SUCCESS)};
        }

        /**
         * @brief 将 Result<void> 转成 syscall 返回包.
         *
         * @param op 日志操作名.
         * @param res 调用结果.
         * @return RetPack 返回包.
         */
        [[nodiscard]]
        RetPack result_void_ret(const char *op,
                                const Result<void> &res) noexcept {
            if (!res.has_value()) {
                loggers::SYSCALL::ERROR("%s失败: err=%s", op,
                                        to_cstring(res.error()));
                return RetPack{.processed = true,
                               .ret0      = false,
                               .ret1      = static_cast<b64>(res.error())};
            }
            return bool_ret(true);
        }

        /**
         * @brief 将 Result<size_t> 转成 syscall 返回包.
         *
         * @param op 日志操作名.
         * @param res 调用结果.
         * @return RetPack 返回包.
         */
        [[nodiscard]]
        RetPack result_value_ret(const char *op,
                                 const Result<size_t> &res) noexcept {
            if (!res.has_value()) {
                loggers::SYSCALL::ERROR("%s失败: err=%s", op,
                                        to_cstring(res.error()));
                return RetPack{.processed = true,
                               .ret0      = 0,
                               .ret1      = static_cast<b64>(res.error())};
            }
            return RetPack{.processed = true,
                           .ret0      = res.value(),
                           .ret1      = static_cast<b64>(ErrCode::SUCCESS)};
        }

        /**
         * @brief 将 Result<bool> 转成 syscall 返回包.
         *
         * @param op 日志操作名.
         * @param res 调用结果.
         * @return RetPack 返回包.
         */
        [[nodiscard]]
        RetPack result_bool_ret(const char *op,
                                const Result<bool> &res) noexcept {
            if (!res.has_value()) {
                loggers::SYSCALL::ERROR("%s失败: err=%s", op,
                                        to_cstring(res.error()));
                return RetPack{.processed = true,
                               .ret0      = 0,
                               .ret1      = static_cast<b64>(res.error())};
            }
            return RetPack{.processed = true,
                           .ret0      = static_cast<b64>(res.value()),
                           .ret1      = static_cast<b64>(ErrCode::SUCCESS)};
        }

        /**
         * @brief 在当前 syscall 所属线程上完成 syscall.
         *
         * @param ret syscall 返回值.
         */
        void complete_syscall(task::TCB *tcb, const RetPack &ret) noexcept {
            if (tcb == nullptr) {
                loggers::SYSCALL::FATAL("无法完成 syscall: 当前线程无效");
                panic("无法完成 syscall");
            }
            tcb->syscall_info.complete(ret);

            loggers::SYSCALL::DEBUG(
                "syscall 返回值: pid=%lu tid=%lu sysno=0x%lx ret0=0x%lx "
                "ret1=0x%lx",
                tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid,
                tcb->syscall_info.syscall_number, ret.ret0, ret.ret1);
            if (tcb->task != nullptr && tcb->task->exiting) {
                loggers::SYSCALL::DEBUG(
                    "syscall 所属线程正在退出, 不重新入队: pid=%lu tid=%lu",
                    tcb->task->pid, tcb->tid);
                return;
            }
            if (tcb->basic_entity.state == ThreadState::WAITING) {
                tcb->basic_entity.state = ThreadState::EMPTY;
                bool wake_ok            = schd::Scheduler::inst().wakeup(tcb);
                if (!wake_ok) {
                    loggers::SYSCALL::FATAL("唤醒已完成 syscall 线程失败");
                    panic("无法唤醒已完成 syscall 线程");
                }
            }
        }

        /**
         * @brief 向串口写字符串.
         *
         * @param str 字符串代理.
         * @param len 输出长度.
         */
        void write_serial(const UString &str, size_t len) noexcept {
            sys_write_serial(str.kbuf(), len);
        }

        void begin_user_syscall(task::TCB *tcb, const ArgPack &args,
                                const task::SyscallContext &context) noexcept {
            assert(tcb != nullptr);
            tcb->syscall_info.begin(args);
            tcb->syscall_info.context = context;
        }

        void handle_sync_user_syscall(task::TCB *tcb,
                                      const ArgPack &args) noexcept {
            assert(tcb != nullptr);
            auto ret = dispatch_sync(util::nnullforce(tcb));
            tcb->syscall_info.complete(ret);
            loggers::SYSCALL::DEBUG(
                "同步 syscall 立即完成: pid=%lu tid=%lu sysno=0x%lx",
                tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid,
                args.syscall_number);
        }

        void handle_async_user_syscall(task::TCB *tcb) noexcept {
            assert(tcb != nullptr);
            auto task = dispatch_async(util::nnullforce(tcb));
            if (tcb->syscall_info.completed()) {
                loggers::SYSCALL::DEBUG(
                    "异步 syscall 立即完成, 不进入协程队列: pid=%lu tid=%lu",
                    tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid);
                return;
            }
            if (task.wait_context().pending()) {
                auto register_res =
                    task::wait::register_syscall_wait(tcb, task.wait_context());
                if (!register_res.has_value()) {
                    loggers::SYSCALL::ERROR(
                        "登记 syscall 等待失败: pid=%lu tid=%lu sysno=0x%lx "
                        "err=%s",
                        tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid,
                        tcb->syscall_info.syscall_number,
                        to_cstring(register_res.error()));
                    auto ret = RetPack{
                        .processed = true,
                        .ret0      = false,
                        .ret1      = static_cast<b64>(register_res.error()),
                    };
                    tcb->syscall_info.complete(ret);
                    task.detach();
                    return;
                }
                loggers::SYSCALL::DEBUG(
                    "syscall 已进入等待, 由等待系统负责恢复: pid=%lu tid=%lu "
                    "sysno=0x%lx",
                    tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid,
                    tcb->syscall_info.syscall_number);
                task.detach();
                return;
            }

            if (tcb->syscall_info.handle == nullptr) {
                tcb->syscall_info.handle = task.handle();
            }
            tcb->basic_entity
                .template flags_set<schd::SchedMeta::FLAGS_NEED_RESCHED>();
            loggers::SYSCALL::INFO(
                "syscall 协程延后到调度前继续执行: pid=%lu tid=%lu "
                "sysno=0x%lx",
                tcb->task != nullptr ? tcb->task->pid : 0, tcb->tid,
                tcb->syscall_info.syscall_number);
            task.detach();
        }
    }  // namespace

    constexpr size_t MAX_SYSCALL_PATH = 256;

    task::SyscallContext *active_context() noexcept {
        return inst_active_context;
    }

    void set_active_context(task::SyscallContext *context) noexcept {
        inst_active_context = context;
    }

    Result<task::TCB *> current_tcb() noexcept {
        if (inst_active_context == nullptr ||
            inst_active_context->tcb == nullptr)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return inst_active_context->tcb;
    }

    Result<task::PCB *> current_pcb() noexcept {
        if (inst_active_context == nullptr ||
            inst_active_context->pcb == nullptr)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return inst_active_context->pcb;
    }

    const char *name_of(b64 sysno) {
        switch (sysno) {
            case SYS_WRITE_SERIAL:        return "SYS_WRITE_SERIAL";
            case SYS_CREATE_PROCESS:      return "SYS_CREATE_PROCESS";
            case SYS_PCB_KILL:            return "SYS_PCB_KILL";
            case SYS_YIELD:               return "SYS_YIELD";
            case SYS_LOG:                 return "SYS_LOG";
            case SYS_FORK:                return "SYS_FORK";
            case SYS_GETPID:              return "SYS_GETPID";
            case SYS_CREATE_THREAD:       return "SYS_CREATE_THREAD";
            case SYS_YIELD_THREAD:        return "SYS_YIELD_THREAD";
            case SYS_EXECVE:              return "SYS_EXECVE";
            case SYS_PCB_MAP:             return "SYS_PCB_MAP";
            case SYS_NOTIF_CREATE:        return "SYS_NOTIF_CREATE";
            case SYS_NOTIF_SIGNAL:        return "SYS_NOTIF_SIGNAL";
            case SYS_NOTIF_UNSIGNAL:      return "SYS_NOTIF_UNSIGNAL";
            case SYS_NOTIF_CHECK:         return "SYS_NOTIF_CHECK";
            case SYS_NOTIF_WAIT:          return "SYS_NOTIF_WAIT";
            case SYS_CAP_CLONE:           return "SYS_CAP_CLONE";
            case SYS_CAP_DOWNGRADE:       return "SYS_CAP_DOWNGRADE";
            case SYS_CAP_DERIVE:          return "SYS_CAP_DERIVE";
            case SYS_CAP_LOOKUP:          return "SYS_CAP_LOOKUP";
            case SYS_CAP_REMOVE:          return "SYS_CAP_REMOVE";
            case SYS_ENDPOINT_CREATE:     return "SYS_ENDPOINT_CREATE";
            case SYS_ENDPOINT_SEND:       return "SYS_ENDPOINT_SEND";
            case SYS_ENDPOINT_RECV:       return "SYS_ENDPOINT_RECV";
            case SYS_ENDPOINT_SEND_ASYNC: return "SYS_ENDPOINT_SEND_ASYNC";
            case SYS_ENDPOINT_RECV_ASYNC: return "SYS_ENDPOINT_RECV_ASYNC";
            case SYS_ENDPOINT_CALL:       return "SYS_ENDPOINT_CALL";
            case SYS_ENDPOINT_REPLY:      return "SYS_ENDPOINT_REPLY";
            case SYS_MEM_CREATE:          return "SYS_MEM_CREATE";
            case SYS_MEM_UNMAP:           return "SYS_MEM_UNMAP";
            case SYS_MEM_RESIZE:          return "SYS_MEM_RESIZE";
            case SYS_MEM_QUERY:           return "SYS_MEM_QUERY";
            default:                      return "UNKNOWN_SYSCALL";
        }
    }

    bool is_suspendable_syscall(b64 sysno) noexcept {
        switch (sysno) {
            case SYS_NOTIF_WAIT:
            case SYS_ENDPOINT_SEND:
            case SYS_ENDPOINT_RECV:
            case SYS_ENDPOINT_CALL: return true;
            default:                return false;
        }
    }

    RetPack dispatch_sync(util::nonnull<task::TCB *> tcb) {
        if (tcb->task == nullptr) {
            return RetPack{
                .processed = false,
                .ret0      = 0,
                .ret1      = static_cast<b64>(ErrCode::INVALID_PARAM),
            };
        }
        task::SyscallContext context{
            .tcb          = tcb,
            .pcb          = tcb->task,
            .tmm          = tcb->task->tmm.get(),
            .trap_context = tcb->syscall_info.context.trap_context,
        };
        set_active_context(&context);
        tcb->syscall_info.context = context;

        ArgPack args = tcb->syscall_info.syscall_args;
        b64 arg0     = args.args[0];
        b64 arg1     = args.args[1];
        b64 arg2     = args.args[2];
        b64 arg3     = args.args[3];
        b64 arg4     = args.args[4];
        b64 arg5     = args.args[5];
        b64 sysno    = args.syscall_number;
        b64 capidx   = args.capidx;

        RetPack ret{
            .processed = true,
            .ret0      = 0,
            .ret1      = static_cast<b64>(ErrCode::SUCCESS),
        };

        switch (sysno) {
            case SYS_WRITE_SERIAL: {
                UString str((VirAddr)arg0, arg1);
                write_serial(str, arg1);
                break;
            }
            case SYS_CREATE_PROCESS: {
                UString path((VirAddr)arg0, MAX_SYSCALL_PATH);
                UBuffer caps_buf((VirAddr)arg1, arg2 * sizeof(CapIdx));
                auto sync_res = caps_buf.sync_from_user();
                if (!sync_res.has_value()) {
                    ret = result_void_ret("同步进程能力列表", sync_res);
                    break;
                }
                UBuffer startup_buf((VirAddr)arg4, arg5);
                if (arg5 != 0) {
                    auto startup_sync_res = startup_buf.sync_from_user();
                    if (!startup_sync_res.has_value()) {
                        ret = result_void_ret("同步进程启动缓冲区",
                                              startup_sync_res);
                        break;
                    }
                }
                ret = result_value_ret(
                    "创建进程",
                    pcb_create_process(capidx, path, std::move(caps_buf), arg2,
                                       arg3, arg5 == 0 ? nullptr : &startup_buf,
                                       arg5));
                break;
            }
            case SYS_CREATE_THREAD: {
                ret = result_value_ret("创建线程",
                                       pcb_create_thread(capidx, VirAddr(arg0),
                                                         VirAddr(arg1), arg2));
                break;
            }
            case SYS_FORK: {
                UBuffer child_cap_buf((VirAddr)arg0, sizeof(CapIdx));
                auto sync_res = child_cap_buf.sync_from_user();
                if (!sync_res.has_value()) {
                    ret = result_void_ret("同步fork返回缓冲区", sync_res);
                    break;
                }
                auto fork_res = pcb_fork(capidx, std::move(child_cap_buf));
                ret           = result_value_ret("fork", fork_res);
                break;
            }
            case SYS_EXECVE: {
                UString path((VirAddr)arg0, MAX_SYSCALL_PATH);
                UBuffer reserved_buf((VirAddr)arg1, arg2 * sizeof(CapIdx));
                auto sync_res = reserved_buf.sync_from_user();
                if (!sync_res.has_value()) {
                    ret = result_void_ret("同步execve保留能力列表", sync_res);
                    break;
                }
                UBuffer startup_buf((VirAddr)arg4, arg5);
                if (arg5 != 0) {
                    auto startup_sync_res = startup_buf.sync_from_user();
                    if (!startup_sync_res.has_value()) {
                        ret = result_void_ret("同步execve启动缓冲区",
                                              startup_sync_res);
                        break;
                    }
                }
                ret = result_bool_ret(
                    "execve",
                    pcb_execve(capidx, path, std::move(reserved_buf), arg2,
                               arg5 == 0 ? nullptr : &startup_buf, arg5));
                break;
            }
            case SYS_PCB_KILL: {
                ret = result_bool_ret("pcb_kill",
                                      pcb_kill(capidx, static_cast<int>(arg0)));
                break;
            }
            case SYS_GETPID: {
                ret = result_value_ret("get_pid", get_pid(capidx));
                break;
            }
            case SYS_NOTIF_SIGNAL: {
                ret = result_bool_ret("设置notification",
                                      notification_signal(capidx, arg0, true));
                break;
            }
            case SYS_NOTIF_UNSIGNAL: {
                ret = result_bool_ret("取消notification",
                                      notification_signal(capidx, arg0, false));
                break;
            }
            case SYS_NOTIF_CHECK: {
                ret = result_bool_ret("查询notification",
                                      check_notification(capidx, arg0));
                break;
            }
            case SYS_NOTIF_CREATE: {
                ret =
                    result_value_ret("创建notification", notification_create());
                break;
            }
            case SYS_CAP_CLONE: {
                ret = result_value_ret("clone capability", cap_clone(capidx));
                break;
            }
            case SYS_CAP_DOWNGRADE: {
                ret = result_bool_ret("downgrade capability",
                                      cap_downgrade(capidx, arg0));
                break;
            }
            case SYS_CAP_DERIVE: {
                ret = result_value_ret("derive capability",
                                       cap_derive(capidx, arg0));
                break;
            }
            case SYS_CAP_LOOKUP: {
                UBuffer info_buf((VirAddr)arg0, sizeof(CapInfo));
                auto lookup_res = sys_cap_lookup(capidx, std::move(info_buf));
                ret = result_bool_ret("lookup capability", lookup_res);
                break;
            }
            case SYS_CAP_REMOVE: {
                ret = result_bool_ret("remove capability", cap_remove(capidx));
                break;
            }
            case SYS_ENDPOINT_CREATE: {
                ret = result_value_ret("创建endpoint", endpoint_create());
                break;
            }
            case SYS_ENDPOINT_SEND_ASYNC: {
                UBuffer packet_buf((VirAddr)arg0, sizeof(MsgPacket));
                auto sync_res = packet_buf.sync_from_user();
                if (!sync_res.has_value()) {
                    ret = result_void_ret("同步异步发送消息包", sync_res);
                    break;
                }
                auto packet = *reinterpret_cast<MsgPacket *>(packet_buf.kbuf());
                ret         = result_bool_ret("异步发送endpoint消息",
                                              endpoint_send_async(capidx, packet));
                break;
            }
            case SYS_ENDPOINT_RECV_ASYNC: {
                UBuffer packet_buf((VirAddr)arg0, sizeof(MsgPacket));
                auto sync_res = packet_buf.sync_from_user();
                if (!sync_res.has_value()) {
                    ret = result_void_ret("同步异步接收消息包", sync_res);
                    break;
                }
                auto packet = *reinterpret_cast<MsgPacket *>(packet_buf.kbuf());
                auto recv_res =
                    endpoint_recv_async(capidx, packet, std::move(packet_buf));
                ret = result_bool_ret("异步接收endpoint消息", recv_res);
                break;
            }
            case SYS_MEM_CREATE: {
                ret = result_value_ret(
                    "mem_create",
                    mem_create(arg0, arg1, arg2,
                               static_cast<cap::MemoryGrowth>(arg3)));
                break;
            }
            case SYS_PCB_MAP: {
                ret = result_bool_ret(
                    "pcb_map",
                    pcb_map(capidx, static_cast<CapIdx>(arg0), VirAddr(arg1),
                            static_cast<PageMan::RWX>(arg2),
                            static_cast<cap::MemoryGrowth>(arg3)));
                break;
            }
            case SYS_MEM_UNMAP: {
                ret = result_bool_ret("mem_unmap",
                                      mem_unmap(capidx, VirAddr(arg0)));
                break;
            }
            case SYS_MEM_RESIZE: {
                ret = result_bool_ret("mem_resize", mem_resize(capidx, arg0));
                break;
            }
            case SYS_MEM_QUERY: {
                UBuffer out_buf((VirAddr)arg0, sizeof(MemQueryRet));
                auto query_res = mem_query(capidx, std::move(out_buf));
                ret            = result_void_ret("mem_query", query_res);
                break;
            }
            case SYS_ENDPOINT_REPLY: {
                UBuffer reply_buf((VirAddr)arg0, sizeof(MsgPacket));
                auto sync_res = reply_buf.sync_from_user();
                if (!sync_res.has_value()) {
                    ret = result_void_ret("同步reply消息包", sync_res);
                    break;
                }
                auto reply_packet =
                    *reinterpret_cast<MsgPacket *>(reply_buf.kbuf());
                ret = result_void_ret("endpoint_reply",
                                      endpoint_reply(capidx, reply_packet));
                break;
            }
            default: {
                ret.processed = false;
                ret.ret0      = 0;
                ret.ret1      = static_cast<b64>(ErrCode::NOT_SUPPORTED);
                loggers::SYSCALL::ERROR("未知的系统调用号: %d", sysno);
                break;
            }
        }
        set_active_context(nullptr);
        return ret;
    }

    task::wait::cotask<void> dispatch_async(util::nonnull<task::TCB *> tcb) {
        if (tcb->task == nullptr) {
            complete_syscall(
                tcb, RetPack{
                         .processed = false,
                         .ret0      = 0,
                         .ret1      = static_cast<b64>(ErrCode::INVALID_PARAM),
                     });
            co_return;
        }

        task::SyscallContext context{
            .tcb          = tcb,
            .pcb          = tcb->task,
            .tmm          = tcb->task->tmm.get(),
            .trap_context = tcb->syscall_info.context.trap_context,
        };
        set_active_context(&context);
        tcb->syscall_info.context = context;

        ArgPack args = tcb->syscall_info.syscall_args;
        b64 arg0     = args.args[0];
        b64 arg1     = args.args[1];
        b64 sysno    = args.syscall_number;
        b64 capidx   = args.capidx;

        RetPack ret{
            .processed = true,
            .ret0      = 0,
            .ret1      = static_cast<b64>(ErrCode::SUCCESS),
        };

        switch (sysno) {
            case SYS_NOTIF_WAIT: {
                auto wait_task        = wait_notification(capidx, arg0);
                Result<bool> wait_res = co_await wait_task;
                ret = result_bool_ret("等待notification", wait_res);
                break;
            }
            case SYS_ENDPOINT_SEND: {
                UBuffer packet_buf((VirAddr)arg0, sizeof(MsgPacket));
                auto sync_res = packet_buf.sync_from_user();
                if (!sync_res.has_value()) {
                    ret = result_void_ret("同步发送消息包", sync_res);
                    break;
                }
                auto packet = *reinterpret_cast<MsgPacket *>(packet_buf.kbuf());
                auto send_task        = endpoint_send_sync(capidx, packet);
                Result<void> send_res = co_await send_task;
                ret = result_void_ret("同步发送endpoint消息", send_res);
                break;
            }
            case SYS_ENDPOINT_RECV: {
                UBuffer packet_buf((VirAddr)arg0, sizeof(MsgPacket));
                auto sync_res = packet_buf.sync_from_user();
                if (!sync_res.has_value()) {
                    ret = result_void_ret("同步接收消息包", sync_res);
                    break;
                }
                auto packet = *reinterpret_cast<MsgPacket *>(packet_buf.kbuf());
                auto recv_task =
                    endpoint_recv_sync(capidx, packet, std::move(packet_buf));
                Result<void> recv_res = co_await recv_task;
                ret = result_void_ret("接收endpoint消息", recv_res);
                break;
            }
            case SYS_ENDPOINT_CALL: {
                UBuffer send_buf((VirAddr)arg0, sizeof(MsgPacket));
                auto send_sync_res = send_buf.sync_from_user();
                if (!send_sync_res.has_value()) {
                    ret = result_void_ret("同步call发送消息包", send_sync_res);
                    break;
                }
                auto send_packet =
                    *reinterpret_cast<MsgPacket *>(send_buf.kbuf());
                UBuffer reply_buf((VirAddr)arg1, sizeof(MsgPacket));
                auto reply_sync_res = reply_buf.sync_from_user();
                if (!reply_sync_res.has_value()) {
                    ret = result_void_ret("同步call回复消息包", reply_sync_res);
                    break;
                }
                auto reply_packet =
                    *reinterpret_cast<MsgPacket *>(reply_buf.kbuf());
                auto call_task = endpoint_call(
                    capidx, send_packet, reply_packet, std::move(reply_buf));
                Result<void> call_res = co_await call_task;
                ret = result_void_ret("endpoint_call", call_res);
                break;
            }
            default: {
                ret.processed = false;
                ret.ret0      = 0;
                ret.ret1      = static_cast<b64>(ErrCode::NOT_SUPPORTED);
                loggers::SYSCALL::ERROR("异步分发遇到未知 syscall: %d", sysno);
                break;
            }
        }

        complete_syscall(tcb, ret);
        set_active_context(nullptr);
        co_return;
    }

    void handle_user_ecall(util::nonnull<task::TCB *> tcb, const ArgPack &args,
                           const task::SyscallContext &context) noexcept {
        Interrupt::sti();
        begin_user_syscall(tcb, args, context);
        if (!is_suspendable_syscall(args.syscall_number)) {
            handle_sync_user_syscall(tcb, args);
            return;
        }
        handle_async_user_syscall(tcb);
    }
}  // namespace syscall
