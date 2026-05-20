/**
 * @file endpoint.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief IPC端点对象
 * @version alpha-1.0.0
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/int.h>
#include <env.h>
#include <guard.h>
#include <logger.h>
#include <mem/vma.h>
#include <object/endpoint.h>
#include <object/perm.h>
#include <task/scheduler.h>
#include <task/wait.h>

#include <cassert>
#include <cstring>

namespace cap {
    namespace {
        /**
         * @brief endpoint_call过程中创建的一对Reply Capability槽位.
         */
        struct ReplySlots {
            /// caller持有的CALLER权限reply cap.
            CapIdx caller  = cap::null;
            /// 临时用于发送给replier的REPLIER|MIGRATE_ONCE权限reply cap.
            CapIdx replier = cap::null;
        };

        /**
         * @brief 检测消息是否有效.
         *
         * @param msg 消息视图
         */
        Result<void> check_msg_valid(const EndpointMsgView &msg) {
            if (msg.msgsz > MAX_MSG_SIZE || msg.capsz > MAX_MSG_CAPS) {
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            }
            if (msg.msgsz != 0 && msg.msgbuf == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            if (msg.capsz != 0 && msg.capidxs == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            void_return();
        }

        /**
         * @brief 从 MsgView 构建一个 EndpointMessage.
         *
         * @param sender_pid 发送者PID, 用于填充EndpointMessage的sender_pid字段
         * @param view 消息视图, 包含消息内容和cap索引列表
         * @return Result<util::owner<EndpointMessage *>> 构建结果,
         * 成功时包含一个EndpointMessage的owner指针; 失败时包含错误码
         */
        Result<util::owner<EndpointMessage *>> build_endpoint_message(
            pid_t sender_pid, const EndpointMsgView &view) {
            propagate(check_msg_valid(view));

            // 创建一个 msg 对象
            util::owner<EndpointMessage *> msg =
                util::owner(new EndpointMessage());
            auto msg_guard = delete_guard(msg);

            if (msg == nullptr) {
                unexpect_return(ErrCode::ALLOCATION_FAILED);
            }

            // 初始化msg
            msg->sender_pid = sender_pid;
            msg->msgsz      = view.msgsz;
            msg->capsz      = view.capsz;
            if (view.msgsz != 0) {
                memcpy(msg->msgbuf, view.msgbuf, view.msgsz);
            }
            for (size_t i = 0; i < view.capsz; ++i) {
                msg->capidxs[i] = view.capidxs[i];
            }

            msg_guard.release();
            return msg;
        }

        /**
         * @brief 为endpoint_call创建并降权caller/replier两端Reply Capability.
         *
         * 失败时会回滚已经插入的cap; 成功后由调用者负责清理两个槽位.
         */
        Result<ReplySlots> create_reply_slots(CHolder *holder) {
            if (holder == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }

            // Reply Payload
            auto reply_payload = util::owner(new ReplyPayload());
            if (reply_payload == nullptr) {
                unexpect_return(ErrCode::ALLOCATION_FAILED);
            }
            auto reply_guard = delete_guard(reply_payload);

            // insert a caller reply object into the holder
            auto caller_slot_res = holder->internal_insert_to_free(
                reply_payload, perm::reply::CALLER);
            if (!caller_slot_res.has_value()) {
                propagate_return(caller_slot_res);
            }
            reply_guard.release();

            // get the caller reply object slot
            CapIdx caller_slot = caller_slot_res.value();
            auto caller_guard  = remove_guard(holder, caller_slot);

            // insert a replier reply object into the holder
            // with MIGRATE_ONCE permission so that the reply object can only be
            // forwarded from the sender side to the receiver side, and can't be
            // forwarded again.
            auto replier_slot_res = holder->internal_insert_to_free(
                reply_payload,
                perm::reply::REPLIER | perm::basic::MIGRATE_ONCE);
            propagate(replier_slot_res);
            CapIdx replier_slot = replier_slot_res.value();

            caller_guard.release();
            return ReplySlots{.caller = caller_slot, .replier = replier_slot};
        }

        // to make sure if the given message is still in the endpoint's message
        // queue used for send_sync to detect if the message has been consumed
        bool message_is_queued(EndpointPayload *payload, EndpointMessage *msg) {
            if (payload == nullptr || msg == nullptr) {
                return false;
            }
            for (auto &message : payload->messages) {
                if (&message == msg) {
                    return true;
                }
            }
            return false;
        }
    }  // namespace

    EndpointPayload::EndpointPayload()
        : send_wait_reason(task::wait::alloc_reason()),
          recv_wait_reason(task::wait::alloc_reason()) {}

    EndpointPayload::~EndpointPayload() {
        while (!messages.empty()) {
            EndpointMessage *msg = &messages.front();
            messages.pop_front();
            delete msg;
        }
    }

    ReplyPayload::ReplyPayload()
        : recv_wait_reason(task::wait::alloc_reason()) {}

    ReplyPayload::~ReplyPayload() {
        delete message;
        message = nullptr;
    }

    Result<bool> EndpointObject::send_async(pid_t sender_pid,
                                            const EndpointMsgView &view) {
        // check the validity of the message and permissions
        // before doing anystate change
        propagate(check_msg_valid(view));
        if (!imply(perm::endpoint::WRITE)) {
            loggers::CAPABILITY::ERROR("Endpoint WRITE权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (view.capsz != 0 && !imply(perm::endpoint::GRANT)) {
            loggers::CAPABILITY::ERROR("Endpoint GRANT权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        // enter the critical section to check if there is any receiver waiting
        // and to enqueue
        InterruptGuard guard;
        guard.enter();

        // 如果有接收者正在等待, 则直接将消息加入消息队列并唤醒接收者
        if (task::wait::has_waiting(_obj->recv_wait_reason)) {
            // build the message to be sent
            auto msg_res = build_endpoint_message(sender_pid, view);
            propagate(msg_res);
            util::owner<EndpointMessage *> msg = msg_res.value();
            _obj->messages.push_back(*msg);
            auto wake_res = task::wait::wake_one(_obj->recv_wait_reason);
            return wake_res.transform(always(true));
        }

        return false;
    }

    util::cotask<Result<void>> EndpointObject::send_sync(
        pid_t sender_pid, const EndpointMsgView &view) {
        // check the validity of the message and permissions
        // before doing anystate change
        co_propagate(check_msg_valid(view));
        if (!imply(perm::endpoint::WRITE)) {
            loggers::CAPABILITY::ERROR("Endpoint WRITE权限不足");
            co_return std::unexpected(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        if (view.capsz != 0 && !imply(perm::endpoint::GRANT)) {
            loggers::CAPABILITY::ERROR("Endpoint GRANT权限不足");
            co_return std::unexpected(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        // build the endpoint message
        auto msg_res = build_endpoint_message(sender_pid, view);
        if (!msg_res.has_value()) {
            co_return std::unexpected(msg_res.error());
        }
        auto msg = msg_res.value();

        // enter the critical section to check if there is any receiver waiting
        // and to enqueue the message if necessary
        {
            InterruptGuard guard;
            guard.enter();

            // enqueue the message and wake up one receiver if there is any
            // waiting we put then into the critical section together to avoid
            // the race condition that the message is consumed by any receiver
            // and we still wake up another receiver
            _obj->messages.push_back(*msg);
            if (task::wait::has_waiting(_obj->recv_wait_reason)) {
                auto wake_res = task::wait::wake_one(_obj->recv_wait_reason);
                // if wake one fails
                if (!wake_res.has_value()) {
                    if (message_is_queued(_obj, msg)) {
                        _obj->messages.remove(*msg);
                    }
                    co_return std::unexpected(wake_res.error());
                }
            }
        }

        // now checked that if the message is consumed
        // then just return
        {
            InterruptGuard guard;
            guard.enter();
            if (!message_is_queued(_obj, msg)) {
                co_return Result<void>{};
            }
        }

        // wait for the message to be consumed
        auto wait_res = co_await task::wait::wait_current(
            _obj->send_wait_reason, {}, [payload = _obj, msg]() {
                return !message_is_queued(payload, msg);
            });
        if (!wait_res.has_value()) {
            InterruptGuard guard;
            guard.enter();
            if (message_is_queued(_obj, msg)) {
                _obj->messages.remove(*msg);
                delete msg;
            }
            co_return std::unexpected(wait_res.error());
        }

        co_return Result<void>{};
    }

    Result<EndpointMessage *> EndpointObject::recv_async() {
        // 接收端必须持有READ权限; 权限不足时不检查队列状态.
        if (!imply(perm::endpoint::READ)) {
            loggers::CAPABILITY::ERROR("Endpoint READ权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        // 在临界区内检查并弹出队首消息, 避免和发送端入队/唤醒并发.
        InterruptGuard guard;
        guard.enter();

        // 非阻塞接收: 当前没有消息时返回nullptr, 由调用者决定是否等待.
        if (_obj->messages.empty()) {
            return static_cast<EndpointMessage *>(nullptr);
        }

        // 消息被接收方取走后, 唤醒一个等待send_sync完成的发送方.
        EndpointMessage *msg = &_obj->messages.front();
        _obj->messages.pop_front();
        auto wake_res = task::wait::wake_one(_obj->send_wait_reason);
        propagate(wake_res);
        return msg;
    }

    util::cotask<Result<EndpointMessage *>> EndpointObject::recv_sync() {
        // 同步接收同样要求READ权限; 权限失败直接返回错误而不进入等待.
        if (!imply(perm::endpoint::READ)) {
            loggers::CAPABILITY::ERROR("Endpoint READ权限不足");
            co_return std::unexpected(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        while (true) {
            // 先尝试走一次非阻塞接收; 如果已有消息, 可避免不必要的挂起.
            auto recv_res = recv_async();
            if (!recv_res.has_value()) {
                co_return std::unexpected(recv_res.error());
            }
            if (recv_res.value() != nullptr) {
                co_return recv_res.value();
            }

            // 队列为空时挂到endpoint接收等待队列, 条件由发送端入队满足.
            auto wait_res = co_await task::wait::wait_current(
                _obj->recv_wait_reason, {}, [payload = _obj]() {
                    return payload != nullptr && !payload->messages.empty();
                });
            if (!wait_res.has_value()) {
                co_return std::unexpected(wait_res.error());
            }
        }
    }

    util::cotask<Result<EndpointMessage *>> EndpointObject::call(
        pid_t sender_pid, CHolder *holder, const EndpointMsgView &msg) {
        // call需要在原消息cap列表末尾追加一个replier cap, 因此必须预留
        // 一个cap槽位.
        auto bounds_res = check_msg_valid(msg);
        co_propagate(bounds_res);
        if (holder == nullptr) {
            co_return std::unexpected(ErrCode::NULLPTR);
        }
        if (msg.capsz >= MAX_MSG_CAPS) {
            co_return std::unexpected(ErrCode::OUT_OF_BOUNDARY);
        }

        // 为本次call创建同一个ReplyPayload的两端: caller端留在本进程
        // 等待回复, replier端随请求消息交给服务端.
        auto slots_res = create_reply_slots(holder);
        co_propagate(slots_res);
        ReplySlots slots = slots_res.value();

        // caller端在call结束后清理; replier端正常情况下会在接收方收取cap时
        // 由MIGRATE_ONCE语义消费, 错误路径仍由guard回滚.
        auto caller_guard  = remove_guard(holder, slots.caller);
        auto replier_guard = remove_guard(holder, slots.replier);

        // EndpointMessage只记录CapIdx, 所以这里复制原cap索引列表, 再追加
        // 本次call临时创建的replier slot.
        CapIdx call_capidxs[MAX_MSG_CAPS]{};
        for (size_t i = 0; i < msg.capsz; ++i) {
            call_capidxs[i] = msg.capidxs[i];
        }
        call_capidxs[msg.capsz] = slots.replier;
        size_t call_capsz       = msg.capsz + 1;

        // 同步发送请求; 返回时表示请求消息已被服务端接收并从endpoint队列取走.
        EndpointMsgView call_msg{
            .msgbuf  = msg.msgbuf,
            .msgsz   = msg.msgsz,
            .capidxs = call_capidxs,
            .capsz   = call_capsz,
        };
        auto send_res = co_await send_sync(sender_pid, call_msg);
        co_propagate(send_res);

        // send_sync成功后, replier cap已经随消息被接收方收取; 本地guard不应
        // 再尝试移除这个slot.
        replier_guard.release();

        // 使用caller端ReplyObject等待服务端写回的回复消息.
        auto caller_cap_res = holder->internal_lookup(slots.caller);
        co_propagate(caller_cap_res);
        ReplyObject reply_obj(util::nnullforce(caller_cap_res.value()));

        auto recv_res = co_await reply_obj.recv_sync();
        co_propagate(recv_res);
        co_return recv_res.value();
    }

    Result<bool> ReplyObject::send_reply(pid_t sender_pid,
                                         const EndpointMsgView &view) {
        // replier端必须持有REPLIER权限; reply payload只允许写入一条消息.
        propagate(check_msg_valid(view));
        if (!imply(perm::reply::REPLIER)) {
            loggers::CAPABILITY::ERROR("Reply REPLIER权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        // 构造回复消息. 与Endpoint消息相同, cap只记录发送方CapIdx,
        // 具体CLONE/MIGRATE/MIGRATE_ONCE处理留到接收方写回时完成.
        util::owner<EndpointMessage *> msg = util::owner(new EndpointMessage());
        auto msg_guard                     = delete_guard(msg);
        if (msg == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        msg->sender_pid = sender_pid;
        msg->msgsz      = view.msgsz;
        msg->capsz      = view.capsz;
        if (view.msgsz != 0) {
            memcpy(msg->msgbuf, view.msgbuf, view.msgsz);
        }
        for (size_t i = 0; i < view.capsz; ++i) {
            msg->capidxs[i] = view.capidxs[i];
        }

        WaitReasonId recv_wait_reason = _obj->recv_wait_reason;

        // 在临界区内检查单条reply槽是否已经被占用, 并安装回复消息.
        {
            InterruptGuard guard;
            guard.enter();

            if (_obj->message != nullptr) {
                return false;
            }

            _obj->message = msg;
            msg_guard.release();
        }

        // 唤醒等待ReplyObject::recv_sync的caller端.
        auto wake_res = task::wait::wake_one(recv_wait_reason);
        return wake_res.transform(always(true));
    }

    Result<void> ReplyObject::reply(pid_t sender_pid, CHolder *holder,
                                    CapIdx reply_cap,
                                    const EndpointMsgView &msg) {
        if (holder == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        // reply是send_reply加一次性cap清理的薄包装; send_reply返回false表示
        // 该ReplyPayload已经有未取走的回复.
        auto send_res = send_reply(sender_pid, msg);
        propagate(send_res);
        if (!send_res.value()) {
            unexpect_return(ErrCode::FAILURE);
        }

        // 服务端本方的replier cap只允许使用一次, 成功写入回复后立即移除.
        auto remove_res = holder->internal_remove(reply_cap);
        propagate(remove_res);
        void_return();
    }

    Result<EndpointMessage *> ReplyObject::recv_async() {
        // check th permissons
        if (!imply(perm::reply::CALLER)) {
            loggers::CAPABILITY::ERROR("Reply CALLER权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        // 取出当前回复消息并清空payload槽位; 无消息时自然返回nullptr.
        InterruptGuard guard;
        guard.enter();

        EndpointMessage *msg = _obj->message;
        _obj->message        = nullptr;
        return msg;
    }

    util::cotask<Result<EndpointMessage *>> ReplyObject::recv_sync() {
        // 同步读取回复前先做权限检查, 避免权限错误线程进入等待队列.
        if (!imply(perm::reply::CALLER)) {
            loggers::CAPABILITY::ERROR("Reply CALLER权限不足");
            co_return std::unexpected(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        while (true) {
            // 先尝试非阻塞读取; 已有回复时直接返回消息所有权.
            auto recv_res = recv_async();
            if (!recv_res.has_value()) {
                co_return std::unexpected(recv_res.error());
            }
            if (recv_res.value() != nullptr) {
                co_return recv_res.value();
            }

            // reply尚未写入时等待对应ReplyPayload的接收条件.
            auto wait_res = co_await task::wait::wait_current(
                _obj->recv_wait_reason, {}, [payload = _obj]() {
                    return payload != nullptr && payload->message != nullptr;
                });
            if (!wait_res.has_value()) {
                co_return std::unexpected(wait_res.error());
            }
        }
    }
}  // namespace cap
