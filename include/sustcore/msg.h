/**
 * @file msg.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 消息包
 * @version alpha-1.0.0
 * @date 2026-05-18
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <sustcore/capability.h>

/**
 * @brief Endpoint IPC消息描述符.
 *
 * 发送时, msgsz/capsz 指向输入长度; 接收时, 内核写回实际收到的
 * 字节数与cap数量. msgbuf 和 caplist 可在对应长度为0时为空.
 */
struct MsgPacket {
    /// 消息数据缓冲区.
    void *msgbuf;
    /// 指向消息字节数的用户态地址.
    size_t *msgsz;
    /// Capability索引列表缓冲区.
    CapIdx *caplist;
    /// 指向Capability数量的用户态地址.
    size_t *capsz;
};