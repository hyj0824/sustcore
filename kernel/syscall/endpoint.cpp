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
#include <task/scheduler.h>
#include <task/task.h>
#include <task/wait.h>

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstring>

namespace syscall {
    namespace {
        [[nodiscard]]
        Result<task::TCB *> running_tcb() noexcept {
            auto *current = schd::Scheduler::inst().current_tcb();
            if (current == nullptr || current->task == nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            return current;
        }

        struct ReplySlots {
            CapIdx caller  = cap::null;
            CapIdx replier = cap::null;
        };

        /**
         * @brief 查找并包装当前CSpace中的EndpointObject.
         */
        [[nodiscard]]
        Result<cap::EndpointObject> endpoint_object(CapIdx capidx) noexcept {
            auto tcb_res = running_tcb();
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
            auto tcb_res = running_tcb();
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
            auto tcb_res = running_tcb();
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
                .msgbuf  = reinterpret_cast<const char *>(msg.msgbuf),
                .msgsz   = msg.msgsz,
                .capidxs = msg.caplist,
                .capsz   = msg.capsz,
            };
        }

        [[nodiscard]]
        Result<ReplySlots> create_reply_slots(cap::CHolder *holder) {
            if (holder == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }

            auto reply_payload = util::owner(new cap::ReplyPayload());
            if (reply_payload == nullptr) {
                unexpect_return(ErrCode::ALLOCATION_FAILED);
            }
            auto reply_guard = delete_guard(reply_payload);

            auto caller_slot_res =
                holder->insert_to_free(reply_payload, perm::reply::CALLER);
            if (!caller_slot_res.has_value()) {
                propagate_return(caller_slot_res);
            }
            reply_guard.release();

            CapIdx caller_slot = caller_slot_res.value();
            auto replier_slot_res = holder->insert_to_free(
                reply_payload,
                perm::reply::REPLIER | perm::basic::MIGRATE_ONCE);
            propagate(replier_slot_res);

            return ReplySlots{.caller = caller_slot,
                              .replier = replier_slot_res.value()};
        }

    }  // namespace

    Result<CapIdx> endpoint_create() {
        auto current_tcb_res = running_tcb();
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

        auto future_res = obj.send(current_pid(), msg_view_res.value());
        propagate(future_res);
        auto future = future_res.value();
        if (future.readable()) {
            auto wait_res = future.value();
            propagate(wait_res);
            return true;
        }
        auto cancel_res = future.cancle();
        propagate(cancel_res);
        return false;
    }

    Result<bool> endpoint_recv_async(CapIdx endpoint, const MsgPacket &packet,
                                     UBuffer &&packet_buf) {
        auto current_tcb_res = running_tcb();
        propagate(current_tcb_res);
        auto holder_res = current_holder(current_tcb_res.value());
        propagate(holder_res);

        auto endpoint_res = endpoint_object(endpoint);
        propagate(endpoint_res);
        cap::EndpointObject obj = endpoint_res.value();

        auto future_res = obj.recv();
        propagate(future_res);
        auto future = future_res.value();
        if (!future.readable()) {
            auto cancel_res = future.cancle();
            propagate(cancel_res);
            return false;
        }

        auto recv_res = future.value();
        propagate(recv_res);
        cap::EndpointMessage *msg = recv_res.value();
        auto msg_guard = delete_guard(util::owner(msg));

        auto *packet_out = reinterpret_cast<MsgPacket *>(packet_buf.kbuf());
        auto write_res =
            write_received_msg(holder_res.value(), msg, packet, packet_out);
        propagate(write_res);
        auto commit_res = packet_buf.commit_to_user();
        propagate(commit_res);

        return true;
    }

    Result<void> endpoint_send_sync(CapIdx endpoint, const MsgPacket &packet) {
        MsgPacket packet_copy = packet;
        auto msg_view_res     = msg_view(packet_copy);
        propagate(msg_view_res);

        // send the message
        auto endpoint_res = endpoint_object(endpoint);
        propagate(endpoint_res);
        cap::EndpointObject obj = endpoint_res.value();

        // wait for the send future to be ready
        auto future_res = obj.send(current_pid(), msg_view_res.value());
        propagate(future_res);
        auto wait_res = wait::wait_for(future_res.value());
        propagate(wait_res);
        void_return();
    }

    Result<void> endpoint_recv_sync(CapIdx endpoint, const MsgPacket &packet,
                                    UBuffer &&packet_buf) {
        auto current_tcb_res = running_tcb();
        propagate(current_tcb_res);
        auto holder_res = current_holder(current_tcb_res.value());
        propagate(holder_res);

        auto endpoint_res = endpoint_object(endpoint);
        propagate(endpoint_res);
        cap::EndpointObject endpoint_obj = endpoint_res.value();

        // receive the message and get the future
        auto future_res = endpoint_obj.recv();
        propagate(future_res);
        // wait for the future to be ready and get the message
        auto recv_res = wait::wait_for(future_res.value());
        propagate(recv_res);

        cap::EndpointMessage *msg = recv_res.value();
        auto msg_guard            = delete_guard(util::owner(msg));

        auto *packet_out = reinterpret_cast<MsgPacket *>(packet_buf.kbuf());
        auto write_res =
            write_received_msg(holder_res.value(), msg, packet, packet_out);
        propagate(write_res);
        auto commit_res = packet_buf.commit_to_user();
        propagate(commit_res);
        void_return();
    }

    Result<void> endpoint_call(CapIdx endpoint, const MsgPacket &send_packet,
                               const MsgPacket &reply_packet,
                               UBuffer &&reply_buf) {
        MsgPacket request_packet = send_packet;
        auto msg_view_res        = msg_view(request_packet);
        propagate(msg_view_res);

        auto current_tcb_res = running_tcb();
        propagate(current_tcb_res);
        auto holder_res = current_holder(current_tcb_res.value());
        propagate(holder_res);
        cap::CHolder *holder = holder_res.value();

        auto endpoint_res = endpoint_object(endpoint);
        propagate(endpoint_res);
        cap::EndpointObject endpoint_obj = endpoint_res.value();

        auto slots_res = create_reply_slots(holder);
        propagate(slots_res);
        ReplySlots slots = slots_res.value();
        auto caller_guard = remove_guard(holder, slots.caller);
        auto replier_guard = remove_guard(holder, slots.replier);

        CapIdx call_capidxs[MAX_MSG_CAPS]{};
        for (size_t i = 0; i < send_packet.capsz; ++i) {
            call_capidxs[i] = send_packet.caplist[i];
        }
        call_capidxs[send_packet.capsz] = slots.replier;
        cap::EndpointMsgView call_msg{
            .msgbuf = reinterpret_cast<const char *>(send_packet.msgbuf),
            .msgsz = send_packet.msgsz,
            .capidxs = call_capidxs,
            .capsz = send_packet.capsz + 1,
        };

        auto caller_cap_res = holder->lookup(slots.caller);
        propagate(caller_cap_res);

        // send and wait
        auto send_future_res = endpoint_obj.send(current_pid(), call_msg);
        propagate(send_future_res);
        auto send_wait_res = wait::wait_for(send_future_res.value());
        propagate(send_wait_res);
        // once we're able to send the message,
        // and the message was successfully received,
        // we'll release the guard cuz the cleanup logic is now moved to the receiver side
        replier_guard.release();
        caller_guard.release();

        // receive the reply
        cap::ReplyObject reply_obj(util::nnullforce(caller_cap_res.value()));
        auto reply_future_res = reply_obj.recv();
        propagate(reply_future_res);
        auto reply_wait_res = wait::wait_for(reply_future_res.value());
        propagate(reply_wait_res);

        auto reply = util::owner(reply_wait_res.value());
        auto reply_guard            = delete_guard(reply);

        auto *reply_out = reinterpret_cast<MsgPacket *>(reply_buf.kbuf());
        auto write_reply_res =
            write_received_msg(holder, reply, reply_packet, reply_out);
        propagate(write_reply_res);
        auto commit_res = reply_buf.commit_to_user();
        propagate(commit_res);
        void_return();
    }

    Result<void> endpoint_reply(CapIdx reply_cap,
                                const MsgPacket &reply_packet) {
        MsgPacket reply_packet_copy = reply_packet;
        auto msg_view_res           = msg_view(reply_packet_copy);
        propagate(msg_view_res);

        auto current_tcb_res = running_tcb();
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
