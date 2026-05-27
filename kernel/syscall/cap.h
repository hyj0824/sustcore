/**
 * @file cap.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 能力相关系统调用
 * @version alpha-1.0.0
 * @date 2026-05-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sustcore/capability.h>
#include <syscall/uaccess.h>

namespace syscall {
    /**
     * @brief 克隆一个 capability.
     *
     * @param src 源 capability 槽位.
     * @return Result<CapIdx> 成功时返回新槽位.
     */
    [[nodiscard]]
    Result<CapIdx> cap_clone(CapIdx src);

    /**
     * @brief 降权一个 capability.
     *
     * @param idx 目标 capability 槽位.
     * @param new_perm 新权限.
     * @return Result<bool> 成功时返回 true.
     */
    [[nodiscard]]
    Result<bool> cap_downgrade(CapIdx idx, b64 new_perm);

    /**
     * @brief 派生一个 capability.
     *
     * @param src 源 capability 槽位.
     * @param new_perm 新权限.
     * @return Result<CapIdx> 成功时返回新槽位.
     */
    [[nodiscard]]
    Result<CapIdx> cap_derive(CapIdx src, b64 new_perm);

    /**
     * @brief 查询 capability 信息并写回调用方提供的内核缓冲区.
     *
     * @param idx 目标 capability 槽位.
     * @param info_buf first-layer syscall 持有并回写的用户缓冲区代理.
     * @return Result<bool> 成功时返回 true.
     */
    [[nodiscard]]
    Result<bool> sys_cap_lookup(CapIdx idx, UBuffer &&info_buf);

    /**
     * @brief 删除一个 capability.
     *
     * @param idx 目标 capability 槽位.
     * @return Result<bool> 成功时返回 true.
     */
    [[nodiscard]]
    Result<bool> cap_remove(CapIdx idx);
}  // namespace syscall
