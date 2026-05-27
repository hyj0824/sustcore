/**
 * @file endpoint.h
 * @brief Endpoint syscalls
 */

#pragma once

#include <sustcore/capability.h>
#include <sustcore/msg.h>
#include <syscall/syscall.h>
#include <syscall/uaccess.h>

#include <cstddef>

namespace syscall {
    /**
     * @brief 创建一个endpoint对象, 并将其cap放入调用者的CSpace中
     * 
     * @param capidx 调用者CSpace中用于存放新cap的索引
     * @return true 创建成功
     * @return false 创建失败
     */
    Result<CapIdx> endpoint_create();
    /**
     * @brief 同步向指定endpoint发送消息
     * 
     * @param endpoint 目标endpoint的cap索引
     * @param packet 用户态MsgPacket地址.
     * @return Result<void> 成功表示消息发送完成.
     */
    [[nodiscard]]
    util::cotask<Result<void>> endpoint_send_sync(CapIdx endpoint,
                                                  const MsgPacket &packet);
    /**
     * @brief 异步向指定endpoint发送消息
     * 
     * @param endpoint 目标endpoint的cap索引
     * @param packet 用户态MsgPacket地址.
     * @return true 消息发送成功
     * @return false 消息发送失败
     */
    [[nodiscard]]
    Result<bool> endpoint_send_async(CapIdx endpoint, const MsgPacket &packet);
    /**
     * @brief 从端点处接收信息
     *
     * @param endpoint 端点cap索引
     * @param packet 用户态MsgPacket地址, 用于写回接收到的消息.
     * @return Result<void> 成功表示消息已接收并写回用户缓冲区.
     */
    [[nodiscard]]
    util::cotask<Result<void>> endpoint_recv_sync(CapIdx endpoint,
                                                  const MsgPacket &packet,
                                                  UBuffer &&packet_buf);
    /**
     * @brief 从端点处接收信息 (异步版本)
     * 
     * @param endpoint 端点cap索引
     * @param packet 用户态MsgPacket地址, 用于写回接收到的消息.
     * @return true 消息接收成功
     * @return false 消息接收失败
     */
    [[nodiscard]]
    Result<bool> endpoint_recv_async(CapIdx endpoint, const MsgPacket &packet,
                                     UBuffer &&packet_buf);

    /**
     * @brief 发起一次同步endpoint调用.
     *
     * 内核会创建ReplyObject, 向sendmsg追加REPLIER|MIGRATE_ONCE权限的reply cap,
     * 发送后阻塞等待CALLER端收到回复, 最后清理调用方reply cap.
     */
    [[nodiscard]]
    util::cotask<Result<void>> endpoint_call(CapIdx endpoint,
                                             const MsgPacket &send_packet,
                                             const MsgPacket &reply_packet,
                                             UBuffer &&reply_buf);
    /**
     * @brief 向ReplyObject写入回复并移除当前CSpace中的reply cap.
     */
    [[nodiscard]]
    Result<void> endpoint_reply(CapIdx reply_cap, const MsgPacket &reply_packet);
}  // namespace syscall
