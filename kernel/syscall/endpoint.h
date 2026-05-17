/**
 * @file endpoint.h
 * @brief Endpoint syscalls
 */

#pragma once

#include <sustcore/addr.h>
#include <sustcore/capability.h>
#include <syscall/syscall.h>

#include <cstddef>

namespace syscall {
    /**
     * @brief 创建一个endpoint对象, 并将其cap放入调用者的CSpace中
     * 
     * @param capidx 调用者CSpace中用于存放新cap的索引
     * @return true 创建成功
     * @return false 创建失败
     */
    bool create_endpoint(CapIdx capidx);
    /**
     * @brief 向指定endpoint发送消息
     * 
     * @param endpoint 目标endpoint的cap索引
     * @param msgbuf 用户缓冲区地址, 包含要发送的消息内容
     * @param msgsz 用户缓冲区大小, 单位为字节
     * @param caplist 用户缓冲区地址, 包含要发送的cap索引列表
     * @param capsz 用户缓冲区大小, 单位为cap索引个数
     * @param blocking 是否阻塞发送, 如果为true且目标endpoint没有准备好接收消息, 则调用将被阻塞直到可以发送; 如果为false则调用将立即返回失败
     * @return true 消息发送成功
     * @return false 消息发送失败
     */
    bool send_msg(CapIdx endpoint, VirAddr msgbuf, size_t msgsz,
                  VirAddr caplist, size_t capsz, bool blocking);
    /**
     * @brief 从端点处接收信息
     *
     * @param endpoint 端点cap索引
     * @param msgbuf 用户缓冲区地址, 用于存放接收到的消息
     * @param msgsz 用户缓冲区大小, 单位为字节
     * @param caplist 用户缓冲区地址, 用于存放接收到的cap索引列表
     * @param capsz 用户缓冲区大小, 单位为cap索引个数
     * @return RetPack 返回结果, 包含是否成功、是否需要defer、错误码等信息
     */
    util::cotask<RetPack> recv_msg_sync(CapIdx endpoint, VirAddr msgbuf,
                                        VirAddr msgsz, VirAddr caplist,
                                        VirAddr capsz);
    /**
     * @brief 从端点处接收信息 (异步版本)
     * 
     * @param endpoint 端点cap索引
     * @param msgbuf 用户缓冲区地址, 用于存放接收到的消息
     * @param msgsz 用户缓冲区大小, 单位为字节
     * @param caplist 用户缓冲区地址, 用于存放接收到的cap索引列表
     * @param capsz 用户缓冲区大小, 单位为cap索引个数
     * @return true 消息接收成功
     * @return false 消息接收失败
     */
    bool recv_msg_async(CapIdx endpoint, VirAddr msgbuf, VirAddr msgsz,
                        VirAddr caplist, VirAddr capsz);
}  // namespace syscall
