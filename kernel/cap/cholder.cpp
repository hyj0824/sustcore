/**
 * @file cholder.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 能力持有者
 * @version alpha-1.0.0
 * @date 2026-02-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cap/cholder.h>
#include <env.h>
#include <guard.h>
#include <logger.h>
#include <object/memory.h>
#include <object/perm.h>
#include <sustcore/capability.h>
#include <task/scheduler.h>
#include <task/task_struct.h>

#include <cassert>

namespace {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static cap::CHolderManager inst_cholder_manager;
    static bool inst_cholder_manager_initialized = false;
}  // namespace

namespace cap {
    void CHolderManager::init() {
        // call the constructor explicitly to ensure the instance is initialized
        // before use
        new (&inst_cholder_manager) CHolderManager();
        inst_cholder_manager_initialized = true;
    }

    bool CHolderManager::initialized() {
        return inst_cholder_manager_initialized;
    }

    CHolderManager &CHolderManager::inst() {
        if (!initialized()) {
            panic("CHolderManager 未初始化!");
        }
        return inst_cholder_manager;
    }

    CHolder::CHolder(size_t id) : _space(), _id(id) {}

    CHolder::~CHolder() {}

    Result<Capability *> CHolder::lookup(CapIdx idx) {
        if (!cap::valid(idx)) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }

        Capability *cap = _space.get(idx);
        if (cap == nullptr) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        return cap;
    }

    Result<void> CHolder::set_slot(CapIdx idx, Capability *cap) {
        if (!cap::valid(idx)) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }

        return _space.set(idx, cap);
    }

    Result<Capability *> CHolder::take_slot(CapIdx idx) {
        if (!cap::valid(idx)) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }

        Capability *cap = _space.take(idx);
        if (cap == nullptr) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        return cap;
    }

    Result<void> CHolder::insert(CapIdx idx, Payload *payload,
                                 b64 permissions) {
        if (!cap::valid(idx)) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }
        if (payload == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (_space.get(idx) != nullptr) {
            unexpect_return(ErrCode::SLOT_BUSY);
        }

        auto *cap = new Capability(payload, permissions);
        if (cap == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        auto set_res = set_slot(idx, cap);
        assert(set_res.has_value());
        return set_res;
    }

    Result<CapIdx> CHolder::insert_to_free(Payload *payload, b64 permissions) {
        auto slot_res = lookup_freeslot();
        propagate(slot_res);
        auto insert_res = insert(slot_res.value(), payload, permissions);
        propagate(insert_res);
        return slot_res.value();
    }

    Result<void> CHolder::remove(CapIdx idx) {
        auto cap_res = lookup(idx);
        propagate(cap_res);

        if (cap_res.value() == nullptr) {
            loggers::CAPABILITY::WARN("尝试移除空槽位 {}", idx);
            void_return();
        }

        return set_slot(idx, nullptr);
    }

    void CHolder::clear() {
        _space.clear();
    }

    Result<CapIdx> CHolder::clone(CapIdx src_idx) {
        auto cap_res = lookup(src_idx);
        propagate(cap_res);

        Capability *src_cap = cap_res.value();
        if (!perm::imply(src_cap->perm(), perm::basic::CLONE)) {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        auto target_res = lookup_freeslot();
        propagate(target_res);
        CapIdx target_idx = target_res.value();

        auto *cloned_cap = src_cap->clone();
        auto set_res     = set_slot(target_idx, cloned_cap);
        if (!set_res.has_value()) {
            delete cloned_cap;
            propagate_return(set_res);
        }
        auto *memory = src_cap->payload_as<MemoryPayload>();
        if (memory != nullptr && !memory->shared &&
            env::inst().tmm() != nullptr)
        {
            auto cow_res = env::inst().tmm()->protect_memory_cow(memory);
            if (!cow_res.has_value()) {
                auto remove_res = set_slot(target_idx, nullptr);
                assert(remove_res.has_value());
                propagate_return(cow_res);
            }
        }
        return target_idx;
    }

    Result<CapIdx> CHolder::derive(CapIdx src_idx, b64 new_perm) {
        auto clone_res = clone(src_idx);
        propagate(clone_res);
        CapIdx target_idx = clone_res.value();

        auto clone_guard = remove_guard(this, target_idx);

        auto cap_res = lookup(target_idx);
        assert(cap_res.has_value());
        auto downgrade_res = cap_res.value()->downgrade(new_perm);
        propagate(downgrade_res);

        clone_guard.release();
        return target_idx;
    }

    Result<void> CHolder::downgrade(CapIdx idx, b64 new_perm) {
        auto cap_res = lookup(idx);
        propagate(cap_res);

        return cap_res.value()->downgrade(new_perm);
    }

    Result<CapIdx> CHolder::transfer_to(CHolder &dst, CapIdx src_idx) {
        auto cap_res = lookup(src_idx);
        propagate(cap_res);
        Capability *src_cap = cap_res.value();

        if (src_cap->imply(perm::basic::CLONE)) {
            auto *cloned_cap = src_cap->clone();
            if (cloned_cap == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }

            auto target_res = dst.lookup_freeslot();
            if (!target_res.has_value()) {
                delete cloned_cap;
                propagate_return(target_res);
            }

            auto set_res = dst.set_slot(target_res.value(), cloned_cap);
            if (!set_res.has_value()) {
                delete cloned_cap;
                propagate_return(set_res);
            }
            return target_res.value();
        }

        if (!src_cap->imply(perm::basic::MIGRATE) &&
            !src_cap->imply(perm::basic::MIGRATE_ONCE))
        {
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }

        b64 delivered_perm = src_cap->perm() & ~perm::basic::MIGRATE_ONCE;
        auto insert_res = dst.insert_to_free(src_cap->payload(), delivered_perm);
        propagate(insert_res);

        auto remove_res = remove(src_idx);
        if (!remove_res.has_value()) {
            auto rollback_res = dst.remove(insert_res.value());
            assert(rollback_res.has_value());
            propagate_return(remove_res);
        }
        return insert_res.value();
    }

    Result<void> CHolder::copy_all_to(CHolder &dst) const {
        ErrCode err = ErrCode::SUCCESS;
        _space.foreach ([&](CapIdx idx, Capability *cap) {
            if (err != ErrCode::SUCCESS) {
                return;
            }
            auto insert_res = dst.insert(idx, cap->payload(), cap->perm());
            if (!insert_res.has_value()) {
                err = insert_res.error();
            }
        });
        if (err != ErrCode::SUCCESS) {
            unexpect_return(err);
        }
        void_return();
    }
}  // namespace cap
