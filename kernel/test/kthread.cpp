/**
 * @file kthread.cpp
 * @brief Kernel thread runtime tests
 */

#include <logger.h>
#include <schd/schdbase.h>
#include <task/scheduler.h>
#include <task/task.h>
#include <task/wait.h>
#include <test/kthread.h>

namespace test::kthread {
    namespace {
        constexpr size_t LOGGER_ROUNDS = 5;
        constexpr size_t WAIT_EVENT_REASON = 0x57414954;

        struct LoggerThreadArgs {
            const char *name;
            size_t rounds;
        };

        struct WaitEventTestState {
            volatile bool waiter_started     = false;
            volatile bool first_wake_observed = false;
            volatile bool event_ready        = false;
            volatile bool waiter_completed   = false;
            volatile bool waiter_passed      = false;
        };

        LoggerThreadArgs logger_a_args{
            .name   = "A",
            .rounds = LOGGER_ROUNDS,
        };
        LoggerThreadArgs logger_b_args{
            .name   = "B",
            .rounds = LOGGER_ROUNDS,
        };
        WaitEventTestState wait_event_state{};

        void logger_thread(void *arg) {
            auto *args = static_cast<LoggerThreadArgs *>(arg);
            for (size_t i = 0; i < args->rounds; ++i) {
                loggers::TASK::INFO("kthread logger %s round %u", args->name,
                                    i);
                schd::Scheduler::inst().yield();
            }
        }

        void wait_event_waiter(void *arg [[maybe_unused]]) {
            wait_event_state.waiter_started = true;
            auto wait_res = task::wait::wait_event(WAIT_EVENT_REASON, []() {
                if (!wait_event_state.first_wake_observed &&
                    task::wait::has_waiting(WAIT_EVENT_REASON))
                {
                    return false;
                }
                if (!wait_event_state.first_wake_observed) {
                    wait_event_state.first_wake_observed = true;
                    return false;
                }
                return wait_event_state.event_ready;
            });

            wait_event_state.waiter_completed = true;
            wait_event_state.waiter_passed    = wait_res.has_value() &&
                                             wait_event_state.first_wake_observed &&
                                             wait_event_state.event_ready;
            loggers::TASK::INFO(
                "wait_event 测试 waiter 结束: res=%s first_wake=%d ready=%d",
                wait_res.has_value() ? "SUCCESS" : to_cstring(wait_res.error()),
                static_cast<int>(wait_event_state.first_wake_observed),
                static_cast<int>(wait_event_state.event_ready));
        }

        void wait_event_waker(void *arg [[maybe_unused]]) {
            while (!wait_event_state.waiter_started ||
                   !task::wait::has_waiting(WAIT_EVENT_REASON))
            {
                schd::Scheduler::inst().yield();
            }

            auto first_wake_res = task::wait::wake_one(WAIT_EVENT_REASON);
            if (!first_wake_res.has_value()) {
                loggers::TASK::ERROR("wait_event 测试首次唤醒失败: %s",
                                     to_cstring(first_wake_res.error()));
                return;
            }
            loggers::TASK::INFO("wait_event 测试首次唤醒数量: %u",
                                static_cast<unsigned>(first_wake_res.value()));

            while (!task::wait::has_waiting(WAIT_EVENT_REASON)) {
                schd::Scheduler::inst().yield();
            }

            wait_event_state.event_ready = true;
            auto second_wake_res = task::wait::wake_one(WAIT_EVENT_REASON);
            if (!second_wake_res.has_value()) {
                loggers::TASK::ERROR("wait_event 测试二次唤醒失败: %s",
                                     to_cstring(second_wake_res.error()));
                return;
            }
            loggers::TASK::INFO("wait_event 测试二次唤醒数量: %u",
                                static_cast<unsigned>(second_wake_res.value()));

            while (!wait_event_state.waiter_completed) {
                schd::Scheduler::inst().yield();
            }

            if (wait_event_state.waiter_passed) {
                loggers::TASK::INFO("wait_event 测试成功完成");
            } else {
                loggers::TASK::ERROR("wait_event 测试失败");
            }
        }
    }  // namespace

    Result<void> start_logger_yield_test() {
        auto thread_a_res = task::TaskManager::inst().create_kernel_thread(
            &logger_thread, &logger_a_args, schd::ClassType::RT);
        propagate(thread_a_res);

        auto thread_b_res = task::TaskManager::inst().create_kernel_thread(
            &logger_thread, &logger_b_args, schd::ClassType::RT);
        propagate(thread_b_res);

        if (!schd::Scheduler::inst().wakeup_new(thread_a_res.value().get())) {
            unexpect_return(ErrCode::CREATION_FAILED);
        }
        if (!schd::Scheduler::inst().wakeup_new(thread_b_res.value().get())) {
            unexpect_return(ErrCode::CREATION_FAILED);
        }

        void_return();
    }

    Result<void> start_wait_event_test() {
        wait_event_state = {};

        auto waiter_res = task::TaskManager::inst().create_kernel_thread(
            &wait_event_waiter, nullptr, schd::ClassType::RT);
        propagate(waiter_res);

        auto waker_res = task::TaskManager::inst().create_kernel_thread(
            &wait_event_waker, nullptr, schd::ClassType::RT);
        propagate(waker_res);

        if (!schd::Scheduler::inst().wakeup_new(waiter_res.value().get())) {
            unexpect_return(ErrCode::CREATION_FAILED);
        }
        if (!schd::Scheduler::inst().wakeup_new(waker_res.value().get())) {
            unexpect_return(ErrCode::CREATION_FAILED);
        }

        void_return();
    }
}  // namespace test::kthread
