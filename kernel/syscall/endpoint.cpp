/**
 * @file endpoint.cpp
 * @brief Endpoint syscalls
 */

#include <cap/cholder.h>
#include <guard.h>
#include <logger.h>
#include <object/endpoint.h>
#include <object/perm.h>
#include <sus/coroutine.h>
#include <sus/nonnull.h>
#include <sus/raii.h>
#include <sustcore/capability.h>
#include <sustcore/errcode.h>
#include <sustcore/msg.h>
#include <syscall/endpoint.h>
#include <syscall/syscall.h>
#include <syscall/uaccess.h>
#include <task/task.h>
#include <task/wait.h>

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstring>

namespace syscall {
    namespace {
        /**
         * @brief 查找并包装当前CSpace中的EndpointObject.
         */
        [[nodiscard]]
        Result<cap::EndpointObject> endpoint_object(CapIdx capidx) noexcept {
            auto tcb_res = current_tcb();
            propagate(tcb_res);
            auto *holder = tcb_res.value()->task->cholder;
            if (holder == nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            auto cap_res = holder->lookup(capidx);
            propagate(cap_res);
            auto *cap = cap_res.value();
            if (cap->payload()->type_id() != PayloadType::ENDPOINT) {
                unexpect_return(ErrCode::TYPE_NOT_MATCHED);
            }
            return cap::EndpointObject(util::nnullforce(cap));
        }

        /**
         * @brief 查找并包装当前CSpace中的ReplyObject.
         */
        [[nodiscard]]
        Result<cap::ReplyObject> reply_object(CapIdx capidx) noexcept {
            auto tcb_res = current_tcb();
            propagate(tcb_res);
            auto *holder = tcb_res.value()->task->cholder;
            if (holder == nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            auto cap_res = holder->lookup(capidx);
            propagate(cap_res);
            auto *cap = cap_res.value();
            if (cap->payload()->type_id() != PayloadType::REPLY) {
                unexpect_return(ErrCode::TYPE_NOT_MATCHED);
            }
            return cap::ReplyObject(util::nnullforce(cap));
        }

        /**
         * @brief 返回当前线程所属进程的pid.
         */
        [[nodiscard]]
        pid_t current_pid() noexcept {
            auto tcb_res = current_tcb();
            return tcb_res.has_value() ? tcb_res.value()->task->pid : 0;
        }

        /**
         * @brief 将收到的消息附带cap插入目标CHolder空闲槽位.
         *
         * 若中途失败, 已插入的cap会被回滚移除.
         */
        [[nodiscard]]
        Result<void> insert_received_caps(cap::CHolder *holder,
                                          cap::EndpointMessage *msg,
                                          CapIdx *out_caps) {
            if (holder == nullptr || msg == nullptr || out_caps == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            CapIdx inserted[MAX_MSG_CAPS]{};
            size_t inserted_count = 0;
            util::Guard cleanup([&]() {
                for (size_t i = 0; i < inserted_count; ++i) {
                    auto remove_res = holder->remove(inserted[i]);
                    assert(remove_res.has_value());
                }
            });

            auto holder_id_res =
                task::TaskManager::inst().lookup_holder_id(msg->sender_pid);
            propagate(holder_id_res);

            auto sender_holder_res =
                cap::CHolderManager::inst().get_holder(holder_id_res.value());
            propagate(sender_holder_res);
            cap::CHolder *sender_holder = sender_holder_res.value();

            for (size_t i = 0; i < msg->capsz; ++i) {
                auto slot_res =
                    sender_holder->transfer_to(*holder, msg->capidxs[i]);
                propagate(slot_res);
                inserted[inserted_count++] = slot_res.value();
                out_caps[i]                = slot_res.value();
            }

            cleanup.release();
            void_return();
        }

        /**
         * @brief 将EndpointMessage写回用户态MsgPacket指定的缓冲区.
         */
        [[nodiscard]]
        Result<void> write_received_msg(cap::CHolder *holder,
                                        cap::EndpointMessage *msg,
                                        const MsgPacket &packet,
                                        MsgPacket *packet_out) {
            if (holder == nullptr || msg == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            if (packet_out == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            if (msg->msgsz > packet.msgsz || msg->capsz > packet.capsz) {
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            }

            CapIdx out_caps[MAX_MSG_CAPS]{};
            if (msg->capsz != 0) {
                auto insert_res = insert_received_caps(holder, msg, out_caps);
                propagate(insert_res);
            }

            memcpy(packet_out->msgbuf, msg->msgbuf, msg->msgsz);
            packet_out->msgsz = msg->msgsz;
            memcpy(packet_out->caplist, out_caps, msg->capsz * sizeof(CapIdx));
            packet_out->capsz = msg->capsz;

            void_return();
        }

        /**
         * @brief 获取当前任务的CHolder.
         */
        [[nodiscard]]
        Result<cap::CHolder *> current_holder(task::TCB *current) noexcept {
            if (current == nullptr || current->task == nullptr ||
                current->task->cholder == nullptr)
            {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            return current->task->cholder;
        }

        /**
         * @brief 直接从扁平MsgPacket构造发送消息视图.
         */
        [[nodiscard]]
        Result<cap::EndpointMsgView> msg_view(MsgPacket &msg) noexcept {
            if (msg.msgsz > MAX_MSG_SIZE || msg.capsz > MAX_MSG_CAPS) {
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            }
            return cap::EndpointMsgView{
                .msgbuf = reinterpret_cast<const char *>(msg.msgbuf),
                .msgsz  = msg.msgsz,
                .capidxs = msg.caplist,
                .capsz  = msg.capsz,
            };
        }

    }  // namespace

    Result<CapIdx> endpoint_create() {
        auto current_tcb_res = current_tcb();
        propagate(current_tcb_res);
        auto holder_res = current_holder(current_tcb_res.value());
        propagate(holder_res);
        auto create_res = holder_res.value()->create<cap::EndpointPayload>();
        propagate(create_res);
        return create_res.value();
    }

    Result<bool> endpoint_send_async(CapIdx endpoint, const MsgPacket &packet) {
        MsgPacket packet_copy = packet;
        auto msg_view_res     = msg_view(packet_copy);
        propagate(msg_view_res);

        auto endpoint_res = endpoint_object(endpoint);
        propagate(endpoint_res);
        cap::EndpointObject obj = endpoint_res.value();

        // to send asynchronously
        auto send_res = obj.send_async(current_pid(), msg_view_res.value());
        propagate(send_res);
        return send_res.value();
    }

    Result<bool> endpoint_recv_async(CapIdx endpoint, const MsgPacket &packet,
                                     UBuffer &&packet_buf) {
        auto current_tcb_res = current_tcb();
        propagate(current_tcb_res);
        auto holder_res = current_holder(current_tcb_res.value());
        propagate(holder_res);

        auto endpoint_res = endpoint_object(endpoint);
        propagate(endpoint_res);
        cap::EndpointObject obj = endpoint_res.value();

        // to receive asynchronously
        auto recv_res = obj.recv_async();
        propagate(recv_res);

        cap::EndpointMessage *msg = recv_res.value();
        if (msg == nullptr) {
            return false;
        }
        auto msg_guard = delete_guard(util::owner(msg));

        auto *packet_out = reinterpret_cast<MsgPacket *>(packet_buf.kbuf());
        auto write_res =
            write_received_msg(holder_res.value(), msg, packet, packet_out);
        propagate(write_res);
        auto commit_res = packet_buf.commit_to_user();
        propagate(commit_res);

        return true;
    }

    util::cotask<Result<void>> endpoint_send_sync(CapIdx endpoint,
                                                  const MsgPacket &packet) {
        MsgPacket packet_copy = packet;
        auto msg_view_res     = msg_view(packet_copy);
        co_propagate(msg_view_res);

        auto endpoint_res = endpoint_object(endpoint);
        co_propagate(endpoint_res);
        cap::EndpointObject obj = endpoint_res.value();

        // to send synchronously
        // so here we have to co_await the send method
        // until the message is actually sent
        auto send_res =
            co_await obj.send_sync(current_pid(), msg_view_res.value());
        co_propagate(send_res);
        co_return Result<void>{};
    }

    util::cotask<Result<void>> endpoint_recv_sync(CapIdx endpoint,
                                                  const MsgPacket &packet,
                                                  UBuffer &&packet_buf) {
        auto current_tcb_res = current_tcb();
        co_propagate(current_tcb_res);
        auto holder_res = current_holder(current_tcb_res.value());
        co_propagate(holder_res);

        auto endpoint_res = endpoint_object(endpoint);
        co_propagate(endpoint_res);
        cap::EndpointObject endpoint_obj = endpoint_res.value();

        // to receive synchronously
        // so here we have to co_await the recv method
        // until a message is actually received
        auto recv_res = co_await endpoint_obj.recv_sync();
        co_propagate(recv_res);

        cap::EndpointMessage *msg = recv_res.value();
        auto msg_guard            = delete_guard(util::owner(msg));

        auto *packet_out = reinterpret_cast<MsgPacket *>(packet_buf.kbuf());
        auto write_res =
            write_received_msg(holder_res.value(), msg, packet, packet_out);
        co_propagate(write_res);
        auto commit_res = packet_buf.commit_to_user();
        co_propagate(commit_res);
        co_return Result<void>{};
    }

    util::cotask<Result<void>> endpoint_call(CapIdx endpoint,
                                             const MsgPacket &send_packet,
                                             const MsgPacket &reply_packet,
                                             UBuffer &&reply_buf) {
        MsgPacket request_packet = send_packet;
        auto msg_view_res        = msg_view(request_packet);
        co_propagate(msg_view_res);

        auto current_tcb_res = current_tcb();
        co_propagate(current_tcb_res);
        auto holder_res = current_holder(current_tcb_res.value());
        co_propagate(holder_res);
        cap::CHolder *holder = holder_res.value();

        auto endpoint_res = endpoint_object(endpoint);
        co_propagate(endpoint_res);
        cap::EndpointObject endpoint_obj = endpoint_res.value();

        auto call_awaiter = endpoint_obj.call(
            current_pid(), holder, msg_view_res.value());
        // do the call and co_await for the reply
        auto call_res = co_await call_awaiter;
        co_propagate(call_res);

        // write the reply message back to user space
        // here we use a guard to ensure the reply message is properly deleted
        // after we're done
        cap::EndpointMessage *reply = call_res.value();
        auto reply_guard            = delete_guard(util::owner(reply));

        // write into the user space
        auto *reply_out = reinterpret_cast<MsgPacket *>(reply_buf.kbuf());
        auto write_reply_res =
            write_received_msg(holder, reply, reply_packet, reply_out);
        co_propagate(write_reply_res);
        auto commit_res = reply_buf.commit_to_user();
        co_propagate(commit_res);
        co_return Result<void>{};
    }

    Result<void> endpoint_reply(CapIdx reply_cap, const MsgPacket &reply_packet) {
        MsgPacket reply_packet_copy = reply_packet;
        auto msg_view_res           = msg_view(reply_packet_copy);
        propagate(msg_view_res);

        auto current_tcb_res = current_tcb();
        propagate(current_tcb_res);
        auto holder_res = current_holder(current_tcb_res.value());
        propagate(holder_res);

        auto reply_res = reply_object(reply_cap);
        propagate(reply_res);
        auto reply_obj = reply_res.value();

        return reply_obj.reply(current_pid(), holder_res.value(), reply_cap,
                               msg_view_res.value());
    }
}  // namespace syscall
