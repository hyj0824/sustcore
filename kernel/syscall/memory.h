/**
 * @file memory.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief Memory capability syscalls
 * @version alpha-1.0.0
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <arch/description.h>
#include <object/memory.h>
#include <sustcore/addr.h>
#include <sustcore/capability.h>

namespace syscall {
    /**
     * @brief 创建 Memory Capability. 
     *
     * 创建时尚无 MemoryObject 可承载权限检查, 因此仅做参数与 holder 插入. 
     *
     * @param idx 目标 capability 槽位. 
     * @param memsz 承诺内存大小. 
     * @param shared 是否共享. 
     * @param continuity 是否要求物理连续. 
     * @param growth 允许的增长/收缩方式. 
     * @return true 创建成功; false 创建失败. 
     */
    bool mem_create(CapIdx idx, size_t memsz, bool shared, bool continuity,
                    cap::MemoryGrowth growth);
    /**
     * @brief 取消当前进程中某 Memory 的映射. 
     *
     * syscall 层只 lookup capability; MAP 权限检查由 MemoryObject 完成. 
     *
     * @param idx Memory capability. 
     * @param vaddr 映射地址. 
     */
    bool mem_unmap(CapIdx idx, VirAddr vaddr);
    /**
     * @brief 调整当前进程中的 Memory 大小. 
     *
     * RESIZE 权限检查和 VMA 同步由 MemoryObject 完成. 
     *
     * @param idx Memory capability. 
     * @param newsz 新大小. 
     */
    bool mem_resize(CapIdx idx, size_t newsz);
    /**
     * @brief Memory 查询 syscall 返回值. 
     */
    struct MemQueryRet {
        /// Memory 承诺大小. 
        size_t memsz;
        /// 当前已分配物理内存大小. 
        size_t allocated;
    };
    /**
     * @brief 查询 Memory 状态. 
     *
     * QUERY 权限检查由 MemoryObject 完成. 
     *
     * @param idx Memory capability. 
     * @return 查询结果; 失败时两个字段均为 0. 
     */
    MemQueryRet mem_query(CapIdx idx);
}  // namespace syscall
