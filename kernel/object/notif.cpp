/**
 * @file notif.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief notification对象
 * @version alpha-1.0.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/int.h>
#include <logger.h>
#include <object/notif.h>
#include <spinlock.h>
#include <task/wait.h>

namespace cap {
    namespace {
        Result<void> resolve_waiters(std::vector<task::wait::Promise<bool>> &waiters,
                                     bool value) {
            ErrCode err = ErrCode::SUCCESS;
            for (auto &promise : waiters) {
                auto set_res = promise.set_value(value);
                if (!set_res.has_value() && err == ErrCode::SUCCESS) {
                    err = set_res.error();
                }
            }
            waiters.clear();
            if (err != ErrCode::SUCCESS) {
                unexpect_return(err);
            }
            void_return();
        }
    }  // namespace

    NotificationPayload::NotificationPayload() : signalbits(0), waiters{} {}

    static Result<void> check_idx(size_t idx) {
        if (idx >= perm::notif::MAX_SIGNALS) {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }
        void_return();
    }

    static Result<void> check_signal_perm(const Capability *cap, size_t idx) {
        auto permbits = perm::notif::perm(cap->perm(), idx);
        if ((permbits & perm::notif::SIGNAL) == 0) {
            loggers::CAPABILITY::ERROR("权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        void_return();
    }

    static Result<void> check_query_perm(const Capability *cap, size_t idx) {
        auto permbits = perm::notif::perm(cap->perm(), idx);
        if ((permbits & perm::notif::QUERY) == 0) {
            loggers::CAPABILITY::ERROR("权限不足");
            unexpect_return(ErrCode::INSUFFICIENT_PERMISSIONS);
        }
        void_return();
    }

    Result<bool> NotificationObject::signal(size_t idx) {
        propagate(check_idx(idx));
        propagate(check_signal_perm(_cap, idx));

        GuardedLock lock(_obj->spinlock);

        _obj->signalbits |= (static_cast<b32>(1U) << idx);
        auto resolve_res = resolve_waiters(_obj->waiters[idx], true);
        propagate(resolve_res);
        return true;
    }

    Result<bool> NotificationObject::unsignal(size_t idx) {
        propagate(check_idx(idx));
        propagate(check_signal_perm(_cap, idx));

        GuardedLock lock(_obj->spinlock);

        _obj->signalbits &= ~(static_cast<b32>(1U) << idx);
        return false;
    }

    Result<bool> NotificationObject::set(size_t idx, bool state) {
        propagate(check_idx(idx));
        propagate(check_signal_perm(_cap, idx));

        GuardedLock lock(_obj->spinlock);
        
        if (state) {
            _obj->signalbits |= (static_cast<b32>(1U) << idx);
            auto resolve_res = resolve_waiters(_obj->waiters[idx], true);
            propagate(resolve_res);
        } else {
            _obj->signalbits &= ~(static_cast<b32>(1U) << idx);
        }
        return state;
    }

    Result<bool> NotificationObject::query(size_t idx) {
        propagate(check_idx(idx));
        propagate(check_query_perm(_cap, idx));

        GuardedLock lock(_obj->spinlock);
        
        return (_obj->signalbits & (static_cast<b32>(1U) << idx)) != 0;
    }

    Result<task::wait::Future<bool>> NotificationObject::wait(size_t idx) {
        propagate(check_idx(idx));
        propagate(check_query_perm(_cap, idx));

        GuardedLock lock(_obj->spinlock);
        
        if ((_obj->signalbits & (static_cast<b32>(1U) << idx)) != 0) {
            task::wait::Promise<bool> promise;
            auto future  = promise.future();
            auto set_res = promise.set_value(true);
            propagate(set_res);
            return future;
        }

        task::wait::Promise<bool> promise;
        auto future = promise.future();
        _obj->waiters[idx].push_back(std::move(promise));
        return future;
    }
}  // namespace cap
