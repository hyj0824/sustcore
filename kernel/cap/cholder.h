/**
 * @file cholder.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 能力持有者
 * @version alpha-1.0.0
 * @date 2026-02-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cap/capability.h>
#include <sus/map.h>
#include <sus/queue.h>
#include <sus/raii.h>
#include <sustcore/capability.h>
#include <sustcore/errcode.h>

#include <unordered_map>
#include <utility>

namespace cap {
    // 能够持有能力的对象称为能力持有者 (Capability Holder)
    // 例如, Process就是一种典型的能力持有者.
    // 又如, VFS也是一种能力持有者, 因为它持有着文件系统的能力 (如根目录的能力).
    class CHolder {
    private:
        CSpace _space;
        size_t _id;
    public:
        CHolder(size_t _id);
        ~CHolder();

        [[nodiscard]]
        constexpr size_t id() const {
            return _id;
        }

        [[nodiscard]]
        constexpr const CSpace &space() const {
            return _space;
        }
        [[nodiscard]]
        constexpr CSpace &space() {
            return _space;
        }

        [[nodiscard]]
        Result<Capability *> access(CapIdx idx) {
            return lookup(idx);
        }

        /**
         * @brief 获取当前 CHolder 中的第一个空闲 capability 槽位.
         *
         * @return Result<CapIdx> 成功返回空闲槽位索引, 失败返回错误码.
         */
        [[nodiscard]]
        Result<CapIdx> lookup_freeslot() {
            return _space.lookup_freeslot();
        }

        /**
         * @brief 在当前 CHolder 中查找指定 capability.
         *
         * @param idx capability 槽位索引.
         * @return Result<Capability *> 成功返回 capability 指针.
         */
        [[nodiscard]]
        Result<Capability *> lookup(CapIdx idx);

        /**
         * @brief 将 payload 插入指定槽位并授予全部权限.
         *
         * @param idx 目标槽位索引.
         * @param payload 要插入的 payload.
         * @return Result<void> 成功返回空结果.
         */
        [[nodiscard]]
        Result<void> insert(CapIdx idx, Payload *payload) {
            assert(payload != nullptr);
            return insert(idx, payload, perm::allperm());
        }

        /**
         * @brief 将 payload 以指定权限插入到指定槽位.
         *
         * @param idx 目标槽位索引.
         * @param payload 要插入的 payload.
         * @param perm 新 capability 的权限位.
         * @return Result<void> 成功返回空结果.
         */
        [[nodiscard]]
        Result<void> insert(CapIdx idx, Payload *payload, b64 perm);

        /**
         * @brief 将payload插入当前CHolder中的第一个空闲槽位.
         *
         * @return 插入成功时返回新cap所在槽位.
         */
        [[nodiscard]]
        Result<CapIdx> insert_to_free(Payload *payload) {
            return insert_to_free(payload, perm::allperm());
        }

        /**
         * @brief 将payload以指定权限插入当前CHolder中的第一个空闲槽位.
         *
         * @return 插入成功时返回新cap所在槽位.
         */
        [[nodiscard]]
        Result<CapIdx> insert_to_free(Payload *payload, b64 perm);

        template <typename PayloadType, typename... Args>
        [[nodiscard]]
        Result<CapIdx> create(Args &&...args) {
            auto *payload = new PayloadType(std::forward<Args>(args)...);
            if (payload == nullptr) {
                unexpect_return(ErrCode::OUT_OF_MEMORY);
            }
            auto insert_res = insert_to_free(payload);
            if (!insert_res.has_value()) {
                delete payload;
                propagate_return(insert_res);
            }
            return insert_res.value();
        }

        [[nodiscard]]
        Result<void> remove(CapIdx idx);

        /**
         * @brief 清空当前 CHolder 中的所有 capability.
         */
        void clear();

        [[nodiscard]]
        Result<CapIdx> clone(CapIdx src_idx);

        [[nodiscard]]
        Result<CapIdx> derive(CapIdx src_idx, b64 new_perm);

        [[nodiscard]]
        Result<void> downgrade(CapIdx idx, b64 new_perm);

        /**
         * @brief 将当前holder中的cap传递到目标holder空闲槽位.
         *
         * CLONE 权限会复制cap; MIGRATE/MIGRATE_ONCE 权限会在目标插入成功后
         * 消费源slot. 目标cap不会携带 MIGRATE_ONCE 位.
         */
        [[nodiscard]]
        Result<CapIdx> transfer_to(CHolder &dst, CapIdx src_idx);

        [[nodiscard]]
        Result<void> copy_all_to(CHolder &dst) const;

    private:
        [[nodiscard]]
        Result<void> set_slot(CapIdx idx, Capability *cap);

        [[nodiscard]]
        Result<Capability *> take_slot(CapIdx idx);
    };

    class CHolderManager {
    private:
        size_t __cur_id = 0;
        constexpr size_t _new_id() {
            return __cur_id++;
        }
        std::unordered_map<size_t, CHolder *> _holders;
        size_t _timestamp = 1;  // 用于记录发送记录的时间戳, 每次发送能力时递增
    public:
        static void init();
        static bool initialized();
        static CHolderManager &inst();

        CHolderManager() = default;

        [[nodiscard]]
        Result<CHolder *> get_holder(size_t _id) const {
            return _holders.at_nt(_id)
                .transform_error(always(ErrCode::OUT_OF_BOUNDARY))
                .transform(unwrap_ref<CHolder *const>());
        }

        template <typename... Args>
        Result<CHolder *> create_holder(Args &&...args) {
            size_t _id  = _new_id();
            auto holder = new CHolder(_id, std::forward<Args>(args)...);
            _holders[_id] = holder;
            return holder;
        }

        Result<void> remove_holder(size_t _id) {
            return get_holder(_id).and_then([&](CHolder *holder) {
                delete holder;
                _holders.erase(_id);
                void_return();
            });
        }

        [[nodiscard]]
        size_t timestamp() {
            return _timestamp++;
        }
    };
}  // namespace cap
