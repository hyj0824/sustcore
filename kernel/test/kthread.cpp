/**
 * @file kthread.cpp
 * @brief Kernel thread runtime tests
 */

#include <logger.h>
#include <schd/schdbase.h>
#include <task/scheduler.h>
#include <task/task.h>
#include <test/kthread.h>

namespace test::kthread {
    namespace {
        constexpr size_t LOGGER_ROUNDS = 5;

        struct LoggerThreadArgs {
            const char *name;
            size_t rounds;
        };

        LoggerThreadArgs logger_a_args{
            .name   = "A",
            .rounds = LOGGER_ROUNDS,
        };
        LoggerThreadArgs logger_b_args{
            .name   = "B",
            .rounds = LOGGER_ROUNDS,
        };

        void logger_thread(void *arg) {
            auto *args = static_cast<LoggerThreadArgs *>(arg);
            for (size_t i = 0; i < args->rounds; ++i) {
                loggers::TASK::INFO("kthread logger %s round %u", args->name,
                                    i);
                schd::Scheduler::inst().yield();
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
}  // namespace test::kthread
