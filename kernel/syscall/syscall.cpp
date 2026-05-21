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

#include <env.h>
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
#include <task/task_struct.h>

#include <cassert>

namespace syscall {
    void write_serial(const UString &str, size_t len) {
        sys_write_serial(str.kbuf(), len);
    }

    constexpr size_t MAX_SYSCALL_PATH = 256;

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

    static RetPack finish_syscall(util::nonnull<Riscv64Context *> ctx,
                                  util::nonnull<task::TCB *> tcb,
                                  RetPack ret) {
        ctx->write_ret(ret);
        ctx->sepc                    += 4;
        tcb->coroutines.done  = true;
        return ret;
    }

    static RetPack bool_ret(bool ok) {
        return RetPack{.processed = true,
                       .ret0      = static_cast<b64>(ok),
                       .ret1      = static_cast<b64>(ErrCode::SUCCESS)};
    }

    static RetPack result_void_ret(const char *op, const Result<void> &res) {
        if (!res.has_value()) {
            loggers::SYSCALL::ERROR("%s失败: err=%s", op,
                                    to_cstring(res.error()));
            return RetPack{.processed = true,
                           .ret0      = false,
                           .ret1      = static_cast<b64>(res.error())};
        }
        return bool_ret(true);
    }

    static RetPack result_value_ret(const char *op, const Result<size_t> &res) {
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

    static RetPack result_bool_ret(const char *op, const Result<bool> &res) {
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

    util::cotask<RetPack> entrance(util::nonnull<Riscv64Context *> ctx,
                                   util::nonnull<task::TCB *> tcb) {
        ArgPack args = ctx->read_args();

        // 参数解包
        b64 arg0 = args.args[0];
        b64 arg1 = args.args[1];
        b64 arg2 = args.args[2];
        b64 arg3 = args.args[3];

        b64 sysno  = args.syscall_number;
        b64 capidx = args.capidx;

        RetPack ret{
            .processed = true,
            .ret0      = 0,
            .ret1      = static_cast<b64>(ErrCode::SUCCESS),
        };

        // capidx (a0) is the primary capability slot; args[] carry
        // operation-specific values starting at a1.
        switch (sysno) {
            // Basic process / memory syscalls.
            case SYS_WRITE_SERIAL: {
                write_serial(UString((VirAddr)arg0, arg1), arg1);
                break;
            }
            case SYS_CREATE_PROCESS: {
                ret = result_value_ret(
                    "创建进程",
                    pcb_create_process(
                        capidx, UString((VirAddr)arg0, MAX_SYSCALL_PATH),
                        VirAddr(arg1), arg2, arg3));
                break;
            }
            case SYS_CREATE_THREAD: {
                ret = result_value_ret(
                    "创建线程",
                    pcb_create_thread(capidx, VirAddr(arg0), VirAddr(arg1),
                                      arg2));
                break;
            }
            case SYS_FORK: {
                ret = result_value_ret("fork", pcb_fork(capidx, VirAddr(arg0)));
                break;
            }
            case SYS_EXECVE: {
                bool current_target = pcb_is_current(capidx);
                auto exec_res        = pcb_execve(
                    capidx, UString((VirAddr)arg0, MAX_SYSCALL_PATH),
                    VirAddr(arg1), arg2);
                if (exec_res.has_value() && exec_res.value()) {
                    if (current_target) {
                        ret.ret0  = ctx->regs[Context::A0_BASE];
                        ret.ret1  = ctx->regs[Context::A0_BASE + 1];
                        ctx->sepc -= 4;
                    } else {
                        ret = bool_ret(true);
                    }
                } else {
                    ret = result_bool_ret("execve", exec_res);
                }
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

            // Notification object operations.
            case SYS_NOTIF_WAIT: {
                ret = result_bool_ret("等待notification",
                                      wait_notification(capidx, arg0));
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
                ret = result_value_ret("创建notification", notification_create());
                break;
            }

            // Generic capability operations.
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
                ret = result_bool_ret("lookup capability",
                                      sys_cap_lookup(capidx, VirAddr(arg0)));
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
            case SYS_ENDPOINT_SEND: {
                auto send_task = endpoint_send_sync(capidx, VirAddr(arg0));
                Result<void> send_res = co_await send_task;
                ret = result_void_ret("同步发送endpoint消息", send_res);
                break;
            }
            case SYS_ENDPOINT_RECV: {
                auto recv_task = endpoint_recv_sync(capidx, VirAddr(arg0));
                Result<void> recv_res = co_await recv_task;
                ret = result_void_ret("接收endpoint消息", recv_res);
                break;
            }
            case SYS_ENDPOINT_SEND_ASYNC: {
                ret = result_bool_ret("异步发送endpoint消息",
                                      endpoint_send_async(capidx,
                                                          VirAddr(arg0)));
                break;
            }
            case SYS_ENDPOINT_RECV_ASYNC: {
                ret = result_bool_ret("异步接收endpoint消息",
                                      endpoint_recv_async(capidx,
                                                          VirAddr(arg0)));
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
                ret = result_void_ret("mem_query",
                                      mem_query(capidx, VirAddr(arg0)));
                break;
            }
            case SYS_ENDPOINT_CALL: {
                auto call_task =
                    endpoint_call(capidx, VirAddr(arg0), VirAddr(arg1));
                Result<void> call_res = co_await call_task;
                ret = result_void_ret("endpoint_call", call_res);
                break;
            }
            case SYS_ENDPOINT_REPLY: {
                ret = result_void_ret("endpoint_reply",
                                      endpoint_reply(capidx, VirAddr(arg0)));
                break;
            }
            default: {
                ret.processed = false;
                loggers::SYSCALL::ERROR("未知的系统调用号: %d", sysno);
                ret.ret0 = 0;
                ret.ret1 = static_cast<b64>(ErrCode::NOT_SUPPORTED);
                break;
            }
        }

        co_return finish_syscall(ctx, tcb, ret);
    };

}  // namespace syscall
