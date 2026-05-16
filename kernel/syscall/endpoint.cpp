/**
 * @file endpoint.cpp
 * @brief Endpoint syscalls
 */

#include <cap/cholder.h>
#include <logger.h>
#include <object/endpoint.h>
#include <sus/nonnull.h>
#include <sus/raii.h>
#include <sustcore/capability.h>
#include <sustcore/errcode.h>
#include <syscall/endpoint.h>
#include <syscall/uaccess.h>
#include <task/scheduler.h>
#include <task/wait.h>

#include <cassert>
#include <cstring>

namespace syscall {
    static Result<cap::EndpointObject> endpoint_object(CapIdx capidx) {
        auto cap_res = cap::CHolder::lookup(capidx);
        propagate(cap_res);
        auto *cap = cap_res.value();
        if (cap->payload()->type_id() != PayloadType::ENDPOINT) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }
        return cap::EndpointObject(util::nnullforce(cap));
    }

    static pid_t current_pid() {
        auto *tcb = schd::Scheduler::inst().current_tcb();
        if (tcb == nullptr || tcb->task == nullptr) {
            return 0;
        }
        return tcb->task->pid;
    }

    bool create_endpoint(CapIdx capidx) {
        auto create_res = cap::CHolder::create<cap::EndpointPayload>(capidx);
        if (!create_res.has_value()) {
            loggers::SYSCALL::ERROR("创建endpoint失败: err=%d",
                                    create_res.error());
            return false;
        }
        return true;
    }

    bool send_msg(CapIdx endpoint, VirAddr msgbuf, size_t msgsz,
                  VirAddr caplist, size_t capsz, bool blocking) {
        // verifying parameters
        if (msgsz > MAX_MSG_SIZE || capsz > MAX_MSG_CAPS) {
            loggers::SYSCALL::ERROR("发送endpoint消息失败: 消息越界 msgsz=%u capsz=%u",
                                    msgsz, capsz);
            return false;
        }
        if (msgsz != 0 && !msgbuf.nonnull()) {
            return false;
        }
        if (capsz != 0 && !caplist.nonnull()) {
            return false;
        }

        char kmsg[MAX_MSG_SIZE]{};
        if (msgsz != 0) {
            UBuffer msg_buf(msgbuf, msgsz);
            msg_buf.sync_from_user();
            memcpy(kmsg, msg_buf.kbuf(), msgsz);
        }

        CapIdx capidxs[MAX_MSG_CAPS]{};
        cap::Capability *caps[MAX_MSG_CAPS]{};
        if (capsz != 0) {
            UBuffer caps_buf(caplist, capsz * sizeof(CapIdx));
            caps_buf.sync_from_user();
            memcpy(capidxs, caps_buf.kbuf(), capsz * sizeof(CapIdx));

            for (size_t i = 0; i < capsz; ++i) {
                auto cap_res = cap::CHolder::lookup(capidxs[i]);
                if (!cap_res.has_value()) {
                    loggers::SYSCALL::ERROR(
                        "发送endpoint消息失败: 附加cap查找失败 idx=%u err=%d",
                        i, cap_res.error());
                    return false;
                }
                caps[i] = cap_res.value();
            }
        }

        // do the send
        auto send_res = endpoint_object(endpoint).and_then(
            [&](cap::EndpointObject obj) {
                return obj.send(current_pid(), kmsg, msgsz, caps, capsz,
                                blocking);
            });
        if (!send_res.has_value()) {
            loggers::SYSCALL::ERROR("发送endpoint消息失败: err=%d",
                                    send_res.error());
            return false;
        }
        return send_res.value();
    }

    static Result<void> insert_received_caps(cap::CHolder *holder,
                                             cap::EndpointMessage *msg,
                                             CapIdx *out_caps) {
        if (holder == nullptr || msg == nullptr || out_caps == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        CapIdx inserted[MAX_MSG_CAPS]{};
        size_t inserted_count = 0;
        // do the cleanup to avoid leaking caps if insertion fails in the middle
        util::Guard cleanup([&]() {
            for (size_t i = 0; i < inserted_count; ++i) {
                auto remove_res = holder->internal_remove(inserted[i]);
                assert(remove_res.has_value());
            }
        });

        for (size_t i = 0; i < msg->capsz; ++i) {
            auto slot_res = holder->internal_lookup_freeslot();
            propagate(slot_res);
            CapIdx slot = slot_res.value();
            auto insert_res = holder->internal_insert(
                slot, msg->caps[i]->payload(), msg->caps[i]->perm());
            propagate(insert_res);
            inserted[inserted_count++] = slot;
            out_caps[i] = slot;
        }

        cleanup.release();
        void_return();
    }

    static Result<void> write_received_msg(cap::CHolder *holder,
                                           cap::EndpointMessage *msg,
                                           VirAddr msgbuf, VirAddr msgsz,
                                           VirAddr caplist, VirAddr capsz) {
        if (holder == nullptr || msg == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (!msgbuf.nonnull() || !msgsz.nonnull() || !capsz.nonnull()) {
            unexpect_return(ErrCode::NULLPTR);
        }

        CapIdx out_caps[MAX_MSG_CAPS]{};
        if (msg->capsz != 0) {
            if (!caplist.nonnull()) {
                unexpect_return(ErrCode::NULLPTR);
            }
            auto insert_res = insert_received_caps(holder, msg, out_caps);
            propagate(insert_res);
        }

        UBuffer out_msg(msgbuf, msg->msgsz);
        if (msg->msgsz != 0) {
            memcpy(out_msg.kbuf(), msg->msgbuf, msg->msgsz);
        }
        out_msg.sync_to_user();

        UBuffer out_msgsz(msgsz, sizeof(size_t));
        memcpy(out_msgsz.kbuf(), &msg->msgsz, sizeof(size_t));
        out_msgsz.sync_to_user();

        UBuffer out_capsz(capsz, sizeof(size_t));
        memcpy(out_capsz.kbuf(), &msg->capsz, sizeof(size_t));
        out_capsz.sync_to_user();

        if (msg->capsz != 0) {
            UBuffer out_caplist(caplist, msg->capsz * sizeof(CapIdx));
            memcpy(out_caplist.kbuf(), out_caps, msg->capsz * sizeof(CapIdx));
            out_caplist.sync_to_user();
        }

        void_return();
    }

    struct RecvPostContext {
        cap::EndpointPayload *endpoint;
        cap::CHolder *holder;
        VirAddr msgbuf;
        VirAddr msgsz;
        VirAddr caplist;
        VirAddr capsz;
        bool ok;
    };

    static bool handle_received_message(RecvPostContext &ctx) {
        if (ctx.endpoint == nullptr) {
            return false;
        }
        if (ctx.endpoint->messages.empty()) {
            return false;
        }

        cap::EndpointMessage *msg = &ctx.endpoint->messages.front();
        ctx.endpoint->messages.pop_front();
        auto msg_guard = util::Guard([&]() { delete msg; });

        auto wake_res = task::wait::wake_one(ctx.endpoint->send_wait_reason);
        if (!wake_res.has_value()) {
            ctx.ok = false;
            return true;
        }

        auto write_res = write_received_msg(ctx.holder, msg, ctx.msgbuf,
                                            ctx.msgsz, ctx.caplist,
                                            ctx.capsz);
        ctx.ok = write_res.has_value();
        if (!write_res.has_value()) {
            loggers::SYSCALL::ERROR("接收endpoint消息失败: 写回失败 err=%d",
                                    write_res.error());
        }
        return true;
    }

    static Result<cap::CHolder *> current_holder() {
        return cap::CHolder::current();
    }

    bool recv_msg_async(CapIdx endpoint, VirAddr msgbuf, VirAddr msgsz,
                        VirAddr caplist, VirAddr capsz) {
        if (!msgbuf.nonnull() || !msgsz.nonnull() || !capsz.nonnull()) {
            return false;
        }

        auto holder_res = current_holder();
        if (!holder_res.has_value()) {
            loggers::SYSCALL::ERROR("接收endpoint消息失败: 当前CSpace不可用 err=%d",
                                    holder_res.error());
            return false;
        }

        auto recv_res = endpoint_object(endpoint).and_then(
            [](cap::EndpointObject obj) { return obj.recv_async(); });
        if (!recv_res.has_value()) {
            loggers::SYSCALL::ERROR("接收endpoint消息失败: err=%d",
                                    recv_res.error());
            return false;
        }

        cap::EndpointMessage *msg = recv_res.value();
        if (msg == nullptr) {
            return false;
        }
        util::Guard msg_guard([&]() { delete msg; });

        auto write_res = write_received_msg(holder_res.value(), msg, msgbuf,
                                            msgsz, caplist, capsz);
        if (!write_res.has_value()) {
            loggers::SYSCALL::ERROR("接收endpoint消息失败: 写回失败 err=%d",
                                    write_res.error());
            return false;
        }

        return true;
    }

    bool recv_msg_sync(CapIdx endpoint, VirAddr msgbuf, VirAddr msgsz,
                       VirAddr caplist, VirAddr capsz) {
        if (!msgbuf.nonnull() || !msgsz.nonnull() || !capsz.nonnull()) {
            return false;
        }

        auto holder_res = current_holder();
        if (!holder_res.has_value()) {
            loggers::SYSCALL::ERROR("接收endpoint消息失败: 当前CSpace不可用 err=%d",
                                    holder_res.error());
            return false;
        }

        auto endpoint_res = endpoint_object(endpoint);
        if (!endpoint_res.has_value()) {
            loggers::SYSCALL::ERROR("接收endpoint消息失败: err=%d",
                                    endpoint_res.error());
            return false;
        }

        cap::EndpointObject endpoint_obj = endpoint_res.value();
        RecvPostContext ctx{
            .endpoint = endpoint_obj.obj(),
            .holder   = holder_res.value(),
            .msgbuf   = msgbuf,
            .msgsz    = msgsz,
            .caplist  = caplist,
            .capsz    = capsz,
            .ok       = true,
        };

        auto recv_res =
            endpoint_obj.recv_sync([&ctx](task::TCB *) {
                return handle_received_message(ctx);
            });
        if (!recv_res.has_value()) {
            loggers::SYSCALL::ERROR("接收endpoint消息失败: err=%d",
                                    recv_res.error());
            return false;
        }
        return recv_res.value() && ctx.ok;
    }
}  // namespace syscall
