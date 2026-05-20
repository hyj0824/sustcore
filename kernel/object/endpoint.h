/**
 * @file endpoint.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief IPC端点对象
 * @version alpha-1.0.0
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cap/capability.h>
#include <object/perm.h>
#include <sus/coroutine.h>
#include <sus/list.h>
#include <sustcore/capability.h>
#include <sustcore/msg.h>
#include <task/task_struct.h>
#include <task/wait.h>

#include <cstddef>

namespace cap {
    class CHolder;

    /**
     * @brief Endpoint发送侧消息视图, 不持有缓冲区或cap数组.
     */
    struct EndpointMsgView {
        const char *msgbuf = nullptr;
        size_t msgsz       = 0;
        CapIdx *capidxs    = nullptr;
        size_t capsz       = 0;
    };

    /**
     * @brief 端点消息
     *
     */
    struct EndpointMessage {
        pid_t sender_pid = 0;
        char msgbuf[MAX_MSG_SIZE]{};
        size_t msgsz = 0;
        CapIdx capidxs[MAX_MSG_CAPS]{};
        size_t capsz = 0;
        util::ListHead<EndpointMessage> list_head{};
    };

    /**
     * @brief Endpoint对象的payload.
     *
     * 包含消息列表与接收/发送等待队列
     *
     */
    struct EndpointPayload : public _PayloadHelper<PayloadType::ENDPOINT> {
        util::IntrusiveList<EndpointMessage, &EndpointMessage::list_head>
            messages = {};
        // 等待队列
        WaitReasonId send_wait_reason;
        WaitReasonId recv_wait_reason;

        EndpointPayload();
        ~EndpointPayload() override;
    };

    /**
     * @brief 一次性调用回复对象的payload.
     *
     * ReplyPayload最多持有一条EndpointMessage, 用于endpoint_call的调用方
     * 阻塞等待endpoint_reply写入结果.
     */
    struct ReplyPayload : public _PayloadHelper<PayloadType::REPLY> {
        EndpointMessage *message = nullptr;
        WaitReasonId recv_wait_reason;

        ReplyPayload();
        ~ReplyPayload() override;
    };

    class EndpointObject : public CapObj<EndpointPayload> {
    public:
        explicit EndpointObject(util::nonnull<Capability *> cap)
            : CapObj<EndpointPayload>(cap) {}

        /**
         * @brief 异步向endpoint写入消息.
         */
        Result<bool> send_async(pid_t sender_pid, const EndpointMsgView &msg);
        /**
         * @brief 同步发送endpoint消息, 直到消息被接收方消费.
         */
        util::cotask<Result<void>> send_sync(pid_t sender_pid,
                                             const EndpointMsgView &msg);
        /**
         * @brief 异步接收endpoint消息.
         *
         */
        Result<EndpointMessage *> recv_async();
        /**
         * @brief 同步接收endpoint消息.
         *
         */
        util::cotask<Result<EndpointMessage *>> recv_sync();
        /**
         * @brief 发起同步调用, 并返回调用方收到的回复消息.
         *
         * 调用过程中会在 holder 中创建 caller/replier Reply Capability,
         * 将 replier cap 附加到请求消息, 发送后等待 caller 端收到回复.
         */
        util::cotask<Result<EndpointMessage *>> call(
            pid_t sender_pid, CHolder *holder, const EndpointMsgView &msg);
    };

    /**
     * @brief Reply Capability对象.
     *
     * 拥有REPLIER权限的一端可写入一次回复; 拥有CALLER权限的一端可阻塞读取.
     */
    class ReplyObject : public CapObj<ReplyPayload> {
    public:
        explicit ReplyObject(util::nonnull<Capability *> cap)
            : CapObj<ReplyPayload>(cap) {}

        /**
         * @brief 写入一次回复消息.
         *
         * 调用者必须持有REPLIER权限. ReplyObject已有消息时返回false.
         */
        Result<bool> send_reply(pid_t sender_pid, const EndpointMsgView &msg);
        /**
         * @brief 发送一次回复并从 holder 中移除本方一次性 reply cap.
         */
        Result<void> reply(pid_t sender_pid, CHolder *holder, CapIdx reply_cap,
                           const EndpointMsgView &msg);
        /**
         * @brief 非阻塞读取ReplyObject中的回复消息.
         *
         * 调用者必须持有CALLER权限; 无消息时返回nullptr.
         */
        Result<EndpointMessage *> recv_async();

        /**
         * @brief 同步接收ReplyObject中的回复消息.
         */
        util::cotask<Result<EndpointMessage *>> recv_sync();
    };
}  // namespace cap
