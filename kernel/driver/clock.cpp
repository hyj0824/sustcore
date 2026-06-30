/**
 * @file clock.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 时钟
 * @version alpha-1.0.0
 * @date 2026-05-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <driver/clock.h>
#include <driver/int/base.h>
#include <env.h>
#include <logger.h>
#include <task/scheduler.h>
#include <task/task.h>
#include <task/wait.h>

namespace driver {
    namespace {
        [[nodiscard]]
        constexpr size_t to_ns_size(units::time value) noexcept {
            return static_cast<size_t>(value.to_nanoseconds());
        }
    }  // namespace

    ExpireActionQueue::ExpireActionQueue() noexcept {
        for (uint16_t index = 0; index < MAX_HEAP_ACTIONS; index++) {
            _slots[index].next_free = index + 1 < MAX_HEAP_ACTIONS
                                          ? index + 1
                                          : ExpireHandle::INVALID_SLOT;
        }
        _free_head = 0;
    }

    bool ExpireActionQueue::empty() const noexcept {
        return _heap_size == 0;
    }

    size_t ExpireActionQueue::size() const noexcept {
        return _heap_size;
    }

    Result<ExpireHandle> ExpireActionQueue::push(
        const ExpireAction &action) noexcept {
        if (_free_head == ExpireHandle::INVALID_SLOT ||
            _heap_size >= MAX_HEAP_ACTIONS)
        {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        uint16_t slot_id = _free_head;
        auto &slot       = _slots[slot_id];
        _free_head       = slot.next_free;

        slot.action     = action;
        slot.heap_index = static_cast<uint16_t>(_heap_size);
        slot.active     = true;

        _heap[_heap_size++] = slot_id;
        shift_up(slot.heap_index);

        return ExpireHandle{
            .slot       = slot_id,
            .generation = slot.generation,
        };
    }

    bool ExpireActionQueue::cancel(ExpireHandle handle) noexcept {
        if (!handle.valid() || handle.slot >= MAX_HEAP_ACTIONS) {
            return false;
        }

        auto &slot = _slots[handle.slot];
        if (!slot.active || slot.generation != handle.generation) {
            return false;
        }

        (void)remove_at(slot.heap_index);
        return true;
    }

    std::optional<ExpireAction> ExpireActionQueue::peek() const noexcept {
        if (_heap_size == 0) {
            return std::nullopt;
        }
        return _slots[_heap[0]].action;
    }

    PopDueResult ExpireActionQueue::pop_due(size_t now_ns) noexcept {
        PopDueResult result{};
        while (_heap_size > 0 && result.count < MAX_DUE_ACTIONS) {
            uint16_t slot_id = _heap[0];
            if (_slots[slot_id].action.expireTime > now_ns) {
                break;
            }
            result.entries[result.count++] = remove_at(0);
        }
        return result;
    }

    ExpireAction ExpireActionQueue::remove_at(size_t heap_index) noexcept {
        uint16_t slot_id = _heap[heap_index];
        auto removed     = _slots[slot_id].action;

        _heap_size--;
        if (heap_index != _heap_size) {
            _heap[heap_index] = _heap[_heap_size];
            _slots[_heap[heap_index]].heap_index =
                static_cast<uint16_t>(heap_index);

            if (heap_index > 0 &&
                later(_heap[(heap_index - 1) / 2], _heap[heap_index]))
            {
                shift_up(heap_index);
            } else {
                shift_down(heap_index);
            }
        }

        auto &slot       = _slots[slot_id];
        slot.active      = false;
        slot.next_free   = _free_head;
        slot.heap_index  = 0;
        slot.generation += 1;
        _free_head       = slot_id;

        return removed;
    }

    void ExpireActionQueue::shift_up(size_t index) noexcept {
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            if (!later(_heap[parent], _heap[index])) {
                break;
            }
            swap_heap_nodes(parent, index);
            index = parent;
        }
    }

    void ExpireActionQueue::shift_down(size_t index) noexcept {
        while (true) {
            size_t left     = index * 2 + 1;
            size_t right    = left + 1;
            size_t smallest = index;

            if (left < _heap_size && later(_heap[smallest], _heap[left])) {
                smallest = left;
            }
            if (right < _heap_size && later(_heap[smallest], _heap[right])) {
                smallest = right;
            }
            if (smallest == index) {
                break;
            }

            swap_heap_nodes(index, smallest);
            index = smallest;
        }
    }

    void ExpireActionQueue::swap_heap_nodes(size_t lhs, size_t rhs) noexcept {
        uint16_t lhs_slot           = _heap[lhs];
        uint16_t rhs_slot           = _heap[rhs];
        _heap[lhs]                  = rhs_slot;
        _heap[rhs]                  = lhs_slot;
        _slots[rhs_slot].heap_index = static_cast<uint16_t>(lhs);
        _slots[lhs_slot].heap_index = static_cast<uint16_t>(rhs);
    }

    bool ExpireActionQueue::later(uint16_t lhs, uint16_t rhs) const noexcept {
        return _slots[lhs].action.expireTime > _slots[rhs].action.expireTime;
    }

    TimeKeeper::TimeKeeper(ClockSource *source, Alarm *alarm) noexcept
        : _source(source), _alarm(alarm), _queue() {
        if (alarm != nullptr) {
            alarm->set_handler(this_call(this, &TimeKeeper::on_timer_irq));
        }
    }

    Result<ExpireHandle> TimeKeeper::enqueue(
        const ExpireAction &action) noexcept {
        if (_source == nullptr || _alarm == nullptr) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        InterruptGuard guard;
        guard.enter();

        size_t now_ns = to_ns_size(_source->to_ns(_source->now()));
        if (action.expireTime <= now_ns) {
            ClockEvent event{
                .last =
                    units::time::from_nanoseconds(static_cast<int64_t>(now_ns)),
                .now =
                    units::time::from_nanoseconds(static_cast<int64_t>(now_ns)),
            };
            run_action(action, event);
            return ExpireHandle{};
        }

        auto old_root   = _queue.peek();
        auto handle_res = _queue.push(action);
        if (!handle_res.has_value()) {
            propagate_return(handle_res);
        }

        auto new_root = _queue.peek();
        if (!old_root.has_value() ||
            (new_root.has_value() &&
             new_root->expireTime != old_root->expireTime))
        {
            rearm_timer_locked();
        }

        loggers::TIMER::DEBUG(
            "TimeKeeper 入队动作: type=%lu deadline=%lu size=%lu",
            static_cast<unsigned long>(action.expireAction),
            static_cast<unsigned long>(action.expireTime),
            static_cast<unsigned long>(_queue.size()));
        return handle_res.value();
    }

    bool TimeKeeper::cancel(ExpireHandle handle) noexcept {
        if (_source == nullptr || _alarm == nullptr) {
            return false;
        }

        InterruptGuard guard;
        guard.enter();
        bool removed = _queue.cancel(handle);
        if (removed) {
            loggers::TIMER::DEBUG(
                "TimeKeeper 取消动作: slot=%u gen=%u size=%lu",
                static_cast<unsigned>(handle.slot),
                static_cast<unsigned>(handle.generation),
                static_cast<unsigned long>(_queue.size()));
            rearm_timer_locked();
        }
        return removed;
    }

    void TimeKeeper::on_timer_irq(const ClockEvent &event) noexcept {
        if (_source == nullptr || _alarm == nullptr) {
            loggers::TIMER::ERROR("TimeKeeper 收到 timer IRQ 时未初始化");
            return;
        }

        PopDueResult due{};
        {
            InterruptGuard guard;
            guard.enter();
            size_t now_ns = to_ns_size(_source->to_ns(_source->now()));
            due           = _queue.pop_due(now_ns);
            rearm_timer_locked();
        }

        for (size_t index = 0; index < due.count; index++) {
            run_action(due.entries[index], event);
        }
    }

    void TimeKeeper::rearm_timer() noexcept {
        if (_source == nullptr || _alarm == nullptr) {
            return;
        }

        InterruptGuard guard;
        guard.enter();
        rearm_timer_locked();
    }

    void TimeKeeper::rearm_timer_locked() noexcept {
        auto next_opt = _queue.peek();
        if (!next_opt.has_value()) {
            loggers::TIMER::DEBUG("TimeKeeper 队列为空, 不重编程 timer");
            return;
        }

        units::time now      = _source->to_ns(_source->now());
        units::time deadline = units::time::from_nanoseconds(
            static_cast<int64_t>(next_opt->expireTime));
        units::time delta = next_opt->expireTime <= to_ns_size(now)
                                ? units::time::from_nanoseconds(1)
                                : deadline - now;
        loggers::TIMER::DEBUG(
            "TimeKeeper 重编程 timer: now=%llu deadline=%llu delta=%llu",
            static_cast<unsigned long long>(now.to_nanoseconds()),
            static_cast<unsigned long long>(deadline.to_nanoseconds()),
            static_cast<unsigned long long>(delta.to_nanoseconds()));
        _alarm->set_next_event(delta);
    }

    void TimeKeeper::run_action(const ExpireAction &action,
                                const ClockEvent &event) noexcept {
        switch (action.expireAction) {
            case expact::SCHD: {
                TimerTickEvent tick_event{
                    .last  = event.last,
                    .now   = event.now,
                    .delta = event.now - event.last,
                };
                schd::Scheduler::inst().do_tick(tick_event);

                units::time period = units::time::from_nanoseconds(
                    static_cast<int64_t>(action.expireArg0));
                auto enqueue_res = enqueue(ExpireAction{
                    .expireTime = static_cast<size_t>(
                        (event.now + period).to_nanoseconds()),
                    .expireAction = expact::SCHD,
                    .expireArg0   = action.expireArg0,
                    .expireArg1   = expact::schd::TICK,
                });
                if (!enqueue_res.has_value()) {
                    loggers::SUSTCORE::ERROR("调度 tick 重新入队失败: err=%s",
                                             to_cstring(enqueue_res.error()));
                }
                return;
            }
            case expact::WAKEUP: {
                loggers::TIMER::INFO("TimeKeeper 普通唤醒: wd=%lu deadline=%llu now=%llu",
                                     static_cast<unsigned long>(action.expireArg0),
                                     static_cast<unsigned long long>(
                                         action.expireTime),
                                     static_cast<unsigned long long>(
                                         event.now.to_nanoseconds()));
                task::process_timeout_wakeup(
                    static_cast<wait::wd_t>(action.expireArg0),
                    action.expireArg1);
                return;
            }
            case expact::TIMEOUT: {
                loggers::TIMER::INFO("TimeKeeper 超时唤醒: tid=%lu deadline=%llu now=%llu",
                                     static_cast<unsigned long>(action.expireArg0),
                                     static_cast<unsigned long long>(
                                         action.expireTime),
                                     static_cast<unsigned long long>(
                                         event.now.to_nanoseconds()));
                task::process_timeout_tcb(
                    static_cast<tid_t>(action.expireArg0));
                return;
            }
            default:
                loggers::TIMER::ERROR(
                    "未知的到期动作类型: %lu",
                    static_cast<unsigned long>(action.expireAction));
                return;
        }
    }
}  // namespace driver
