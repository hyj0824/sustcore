/**
 * @file memory.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief Memory capability syscalls
 * @version alpha-1.0.0
 * @date 2026-05-17
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cap/cholder.h>
#include <cap/permission.h>
#include <env.h>
#include <logger.h>
#include <mem/gfp.h>
#include <mem/vma.h>
#include <object/memory.h>
#include <syscall/memory.h>

namespace {
    /**
     * @brief 查找当前进程 capability 空间中的 Memory payload. 
     */
    Result<cap::MemoryPayload *> lookup_memory(
        CapIdx idx, cap::Capability **out_cap = nullptr) {
        auto cap_res = cap::CHolder::lookup(idx);
        propagate(cap_res);
        if (out_cap != nullptr) {
            *out_cap = cap_res.value();
        }
        auto *memory = cap_res.value()->payload_as<cap::MemoryPayload>();
        if (memory == nullptr) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }
        return memory;
    }

}  // namespace

namespace syscall {
    bool mem_create(CapIdx idx, size_t memsz, bool shared, bool continuity,
                    cap::MemoryGrowth growth) {
        if (shared && growth != cap::MemoryGrowth::FIXED) {
            return false;
        }
        auto payload =
            new cap::MemoryPayload(memsz, shared, continuity, growth);
        auto insert_res =
            cap::CHolder::current().and_then([&](cap::CHolder *holder) {
                return holder->internal_insert(idx, payload);
            });
        if (!insert_res.has_value()) {
            delete payload;
            loggers::SYSCALL::ERROR("mem_create失败: err=%d",
                                    insert_res.error());
            return false;
        }
        return true;
    }

    bool mem_unmap(CapIdx idx, VirAddr vaddr) {
        cap::Capability *cap = nullptr;
        auto memory_res      = lookup_memory(idx, &cap);
        if (!memory_res.has_value()) {
            return false;
        }
        auto *tmm = env::inst().tmm();
        if (tmm == nullptr) {
            return false;
        }
        cap::MemoryObject obj(util::nnullforce(cap));
        auto remove_res = obj.unmap_from(*tmm, vaddr);
        return remove_res.has_value();
    }

    bool mem_resize(CapIdx idx, size_t newsz) {
        cap::Capability *cap = nullptr;
        auto memory_res      = lookup_memory(idx, &cap);
        if (!memory_res.has_value()) {
            return false;
        }
        cap::MemoryObject obj(util::nnullforce(cap));
        auto *tmm       = env::inst().tmm();
        auto resize_res = obj.resize_in(tmm, newsz);
        return resize_res.has_value();
    }

    MemQueryRet mem_query(CapIdx idx) {
        cap::Capability *cap = nullptr;
        auto memory_res      = lookup_memory(idx, &cap);
        if (!memory_res.has_value()) {
            return {0, 0};
        }
        cap::MemoryObject obj(util::nnullforce(cap));
        auto query_res = obj.query();
        if (!query_res.has_value()) {
            return {0, 0};
        }
        return {query_res.value().memsz, query_res.value().allocated};
    }
}  // namespace syscall
