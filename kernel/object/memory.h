/**
 * @file memory.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief Memory capability object
 * @version alpha-1.0.0
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <arch/description.h>
#include <cap/capability.h>
#include <sus/list.h>
#include <sustcore/addr.h>

class TaskMemoryManager;

namespace cap {
    /**
     * @brief Memory Capability 已分配物理页记录. 
     *
     * addr 是物理页起始地址, offset 是该页在 Memory payload 中的页对齐偏移. 
     */
    struct PhyPage {
        /// 物理页起始地址. 
        PhyAddr addr;
        /// 该物理页在 Memory payload 内的偏移. 
        size_t offset;

        /**
         * @brief 比较两个物理页记录是否完全相同. 
         */
        constexpr bool operator==(const PhyPage &other) const noexcept {
            return addr == other.addr && offset == other.offset;
        }
    };

    /**
     * @brief Memory Capability 支持的 VMA 增长/收缩方向. 
     *
     * FIXED 表示不可调整; FLEXUP/FLEXDOWN 是对应 grow/shrink 位的组合. 
     */
    enum class MemoryGrowth : b64 {
        FIXED       = 0,
        GROW_UP     = 0b0001,
        GROW_DOWN   = 0b0010,
        SHRINK_UP   = 0b0100,
        SHRINK_DOWN = 0b1000,
        FLEXUP      = GROW_UP | SHRINK_UP,
        FLEXDOWN    = GROW_DOWN | SHRINK_DOWN
    };

    /**
     * @brief 测试两个 MemoryGrowth 位集合是否有交集. 
     */
    constexpr bool operator&(MemoryGrowth lhs, MemoryGrowth rhs) {
        return (static_cast<b64>(lhs) & static_cast<b64>(rhs)) != 0;
    }

    /**
     * @brief Memory Capability 的 payload. 
     *
     * MemoryPayload 表示一段承诺大小的物理内存. 物理页按需分配, 
     * 由 phy_pages 记录已经实际分配的页. VMA 只持有该 payload 的引用, 
     * 物理页生命周期由 payload 统一管理. 
     */
    struct MemoryPayload : public _PayloadHelper<PayloadType::MEMORY> {
        /// 承诺的内存大小, 单位为字节. 
        size_t memsz;
        /// 是否在 clone 时共享同一 payload 和物理页. 
        bool shared;
        /// 是否要求物理页连续; 为 true 时 map/resize 需要连续分配. 
        bool continuity;
        /// 允许的增长/收缩方式. 
        MemoryGrowth growth;
        /// 已实际分配的物理页列表. 
        util::ArrayList<PhyPage> phy_pages;

        /**
         * @brief 构造 Memory payload. 
         *
         * @param memsz 承诺的内存大小. 
         * @param shared 是否共享. 
         * @param continuity 是否要求物理连续. 
         * @param growth 允许的增长/收缩方式. 
         */
        MemoryPayload(size_t memsz, bool shared, bool continuity,
                      MemoryGrowth growth);
        ~MemoryPayload() override = default;

        /**
         * @brief 释放 payload 及其持有的所有物理页. 
         */
        void destruct() override;
        /**
         * @brief 按 Memory clone 语义复制 payload. 
         *
         * shared Memory 返回自身; 非 shared Memory 创建新 payload, 
         * 并让已分配页通过 GFP 引用计数进入 COW 共享状态. 
         *
         * @return 克隆后的 payload. 
         */
        Payload *clone_payload() override;

        /**
         * @brief 查询指定偏移对应的已分配物理页. 
         *
         * @param offset Memory 内偏移, 可以非页对齐. 
         * @return 已分配物理页地址; 未分配返回 PAGE_NOT_PRESENT. 
         */
        Result<PhyAddr> lookup_page(size_t offset) const;
        /**
         * @brief 确保指定偏移对应的物理页存在. 
         *
         * 未分配时会懒分配一个零页并加入 phy_pages. 
         *
         * @param offset Memory 内偏移, 可以非页对齐. 
         * @return 物理页地址. 
         */
        Result<PhyAddr> ensure_page(size_t offset);
        /**
         * @brief 将指定偏移对应的物理页替换为新页. 
         *
         * 用于 COW 写保护异常处理. 旧页引用会被释放. 
         *
         * @param offset Memory 内偏移, 可以非页对齐. 
         * @param new_addr 新物理页地址. 
         */
        Result<void> replace_page(size_t offset, PhyAddr new_addr);
        /**
         * @brief 调整 Memory 承诺大小. 
         *
         * 按 growth 约束执行. 收缩会释放超出范围页; continuity 为 true
         * 时会重新分配连续物理区并复制保留内容. 
         *
         * @param newsz 新大小. 
         */
        Result<void> resize(size_t newsz);
        /**
         * @brief 释放从指定偏移开始的已分配物理页. 
         *
         * @param offset 起始偏移. 
         */
        void release_pages_from(size_t offset);
        /**
         * @brief 查询当前已分配的物理内存大小. 
         *
         * @return 已分配页数乘以 PAGESIZE. 
         */
        size_t allocated_size() const;
    };

    /**
     * @brief Memory Capability 的操作封装. 
     *
     * 所有依赖 Memory 权限位的操作都在该对象内完成权限校验, 
     * syscall 层只负责 lookup capability 并构造 MemoryObject. 
     */
    class MemoryObject : public CapObj<MemoryPayload> {
    public:
        /**
         * @brief 从 capability 构造 MemoryObject. 
         *
         * @param cap MEMORY 类型 capability. 
         */
        explicit MemoryObject(const util::nonnull<Capability *> &cap)
            : CapObj<MemoryPayload>(cap) {}

        /**
         * @brief 将 Memory 映射进指定任务内存空间. 
         *
         * 校验 MAP、READ/WRITE/EXEC、FLEXUP/FLEXDOWN 权限, 
         * 并根据 Memory 属性创建关联 VMA. 
         *
         * @param tmm 目标任务内存管理器. 
         * @param vaddr 映射起始虚拟地址. 
         * @param rwx 页权限. 
         * @param growth VMA 请求的增长方式. 
         */
        Result<void> map_into(TaskMemoryManager &tmm, VirAddr vaddr,
                              PageMan::RWX rwx, MemoryGrowth growth) const;
        /**
         * @brief 从指定任务内存空间取消该 Memory 的映射. 
         *
         * @param tmm 目标任务内存管理器. 
         * @param vaddr 要取消映射的 VMA 地址. 
         */
        Result<void> unmap_from(TaskMemoryManager &tmm, VirAddr vaddr) const;
        /**
         * @brief 调整 Memory 大小并同步相关 VMA. 
         *
         * @param tmm 当前进程的任务内存管理器; 可为空, 仅调整 payload. 
         * @param newsz 新大小. 
         */
        Result<void> resize_in(TaskMemoryManager *tmm, size_t newsz) const;
        /**
         * @brief Memory 查询结果. 
         */
        struct QueryResult {
            /// 承诺大小. 
            size_t memsz;
            /// 当前已分配物理内存大小. 
            size_t allocated;
        };
        /**
         * @brief 查询 Memory 状态. 
         *
         * 校验 QUERY 权限. 
         *
         * @return 承诺大小和已分配大小. 
         */
        [[nodiscard]]
        Result<QueryResult> query() const;
    };
}  // namespace cap
