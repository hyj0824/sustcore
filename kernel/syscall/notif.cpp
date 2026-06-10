/**
 * @file notif.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief notification相关系统调用
 * @version alpha-1.0.0
 * @date 2026-05-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cap/cholder.h>
#include <logger.h>
#include <object/notif.h>
#include <sus/nonnull.h>
#include <sustcore/capability.h>
#include <sustcore/errcode.h>
#include <syscall/notif.h>
#include <task/scheduler.h>
namespace syscall {
    namespace {
        [[nodiscard]]
        Result<task::TCB *> running_tcb() noexcept {
            auto *current = schd::Scheduler::inst().current_tcb();
            if (current == nullptr || current->task == nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            return current;
        }
    }  // namespace

    /**
     * @brief 获取当前线程的 capability holder.
     */
    static Result<cap::CHolder *> current_holder() {
        auto current_tcb_res = running_tcb();
        propagate(current_tcb_res);
        auto *current_tcb = current_tcb_res.value();
        if (current_tcb->task == nullptr ||
            current_tcb->task->cholder == nullptr)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        return current_tcb->task->cholder;
    }

    static Result<cap::NotificationObject> notif_object(CapIdx capidx) {
        // 统一能力查找与类型校验, 减少各 handler 的重复逻辑
        auto holder_res = current_holder();
        propagate(holder_res);
        auto cap_res = holder_res.value()->lookup(capidx);
        propagate(cap_res);
        auto *cap = cap_res.value();
        if (cap->payload()->type_id() != PayloadType::NOTIF) {
            unexpect_return(ErrCode::TYPE_NOT_MATCHED);
        }
        return cap::NotificationObject(util::nnullforce(cap));
    }

    Result<bool> wait_notification(CapIdx capidx, size_t idx) {
        auto notif_res = notif_object(capidx);
        propagate(notif_res);
        auto future_res = notif_res.value().wait(idx);
        propagate(future_res);
        auto wait_res = wait::wait_for(future_res.value());
        propagate(wait_res);
        return wait_res.value();
    }

    Result<bool> notification_signal(CapIdx capidx, size_t idx, bool state) {
        auto set_res = notif_object(capidx).and_then(
            [idx, state](cap::NotificationObject obj) {
                return obj.set(idx, state);
            });
        propagate(set_res);
        return set_res.value();
    }

    Result<bool> check_notification(CapIdx capidx, size_t idx) {
        auto query_res = notif_object(capidx).and_then(
            [idx](cap::NotificationObject obj) { return obj.query(idx); });
        propagate(query_res);
        return query_res.value();
    }

    Result<CapIdx> notification_create() {
        auto holder_res = current_holder();
        propagate(holder_res);
        auto create_res =
            holder_res.value()->create<cap::NotificationPayload>();
        propagate(create_res);
        return create_res.value();
    }
}  // namespace syscall
