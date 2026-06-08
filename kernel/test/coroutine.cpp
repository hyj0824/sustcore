/**
 * @file coroutine.cpp
 * @brief util::cotask lifecycle tests
 */

#include <sus/coroutine.h>
#include <task/wait.h>
#include <test/coroutine.h>

#include <utility>

namespace test::coroutine {
    namespace {
        struct FrameProbe {
            static int live_count;
            static int destruct_count;

            FrameProbe() {
                ++live_count;
            }

            FrameProbe(const FrameProbe&)            = delete;
            FrameProbe& operator=(const FrameProbe&) = delete;

            ~FrameProbe() {
                --live_count;
                ++destruct_count;
            }

            static void reset() {
                live_count     = 0;
                destruct_count = 0;
            }
        };

        int FrameProbe::live_count     = 0;
        int FrameProbe::destruct_count = 0;

        struct ReturnProbe {
            static int live_count;
            static int destruct_count;

            int value = 0;

            ReturnProbe() {
                ++live_count;
            }

            explicit ReturnProbe(int v) : value(v) {
                ++live_count;
            }

            ReturnProbe(const ReturnProbe& other) : value(other.value) {
                ++live_count;
            }

            ReturnProbe(ReturnProbe&& other) noexcept : value(other.value) {
                other.value = -1;
                ++live_count;
            }

            ReturnProbe& operator=(const ReturnProbe& other) {
                value = other.value;
                return *this;
            }

            ReturnProbe& operator=(ReturnProbe&& other) noexcept {
                value       = other.value;
                other.value = -1;
                return *this;
            }

            ~ReturnProbe() {
                --live_count;
                ++destruct_count;
            }

            static void reset() {
                live_count     = 0;
                destruct_count = 0;
            }
        };

        int ReturnProbe::live_count     = 0;
        int ReturnProbe::destruct_count = 0;

        struct ManualSuspendGate {
            std::coroutine_handle<> suspended = nullptr;

            struct awaiter {
                ManualSuspendGate* gate = nullptr;

                [[nodiscard]]
                bool await_ready() const noexcept {
                    return false;
                }

                bool await_suspend(std::coroutine_handle<> handle) const noexcept {
                    gate->suspended = handle;
                    return true;
                }

                void await_resume() const noexcept {}
            };

            awaiter operator co_await() noexcept {
                return awaiter{this};
            }
        };

        util::cotask<void> make_void_task() {
            FrameProbe probe;
            (void)probe;
            co_return;
        }

        util::cotask<ReturnProbe> make_value_task(int value) {
            FrameProbe probe;
            (void)probe;
            co_return ReturnProbe(value);
        }

        util::cotask<int> normal_child(ManualSuspendGate& gate,
                                       bool& resumed_flag) {
            FrameProbe probe;
            (void)probe;
            co_await gate;
            resumed_flag = true;
            co_return 7;
        }

        util::cotask<int> await_normal_child(ManualSuspendGate& gate,
                                             bool& child_ran) {
            auto child = normal_child(gate, child_ran);
            auto value = co_await child;
            co_return value + 9;
        }

        task::wait::cotask<int> wait_leaf(size_t reason) {
            auto wait_res = co_await task::wait::FutureAwaiter(
                reason, {}, []() { return false; });
            if (!wait_res.has_value()) {
                co_return -1;
            }
            co_return 11;
        }

        task::wait::cotask<int> wait_parent(size_t reason) {
            auto value = co_await wait_leaf(reason);
            co_return value + 1;
        }

        task::wait::cotask<int> wait_after_normal_suspend() {
            auto child = make_value_task(5);
            auto value = co_await child;
            co_return value.value;
        }

        task::wait::cotask<Result<int>> co_await_ready_future() {
            task::wait::Promise<int> promise;
            auto future  = promise.future();
            auto set_res = promise.set_value(31);
            if (!set_res.has_value()) {
                co_return std::unexpected(set_res.error());
            }
            auto value_res = co_await future;
            co_return value_res;
        }

        task::wait::cotask<Result<int>> co_await_result_future() {
            task::wait::Promise<Result<int>> promise;
            auto future  = promise.future();
            auto set_res = promise.set_value(Result<int>{47});
            if (!set_res.has_value()) {
                co_return std::unexpected(set_res.error());
            }
            auto value_res = co_await future;
            co_return value_res;
        }

        class CaseScopedVoidTask : public TestCase {
        public:
            CaseScopedVoidTask()
                : TestCase("cotask<void> 作用域析构回收完成帧") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                FrameProbe::reset();
                {
                    auto task = make_void_task();
                    ttest(task.valid());
                    ttest(task.done());
                    ttest(FrameProbe::live_count == 0);
                    ttest(FrameProbe::destruct_count == 1);
                }
                ttest(FrameProbe::live_count == 0);
                ttest(FrameProbe::destruct_count == 1);
            }
        };

        class CaseScopedValueTask : public TestCase {
        public:
            CaseScopedValueTask()
                : TestCase("cotask<T> 保留结果并由析构释放帧") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                FrameProbe::reset();
                ReturnProbe::reset();
                {
                    auto task = make_value_task(42);
                    ttest(task.valid());
                    ttest(task.done());
                    auto result = task.result();
                    ttest(result.value == 42);
                    ttest(FrameProbe::destruct_count == 1);
                    ttest(ReturnProbe::live_count >= 1);
                }
                ttest(FrameProbe::live_count == 0);
                ttest(FrameProbe::destruct_count == 1);
                ttest(ReturnProbe::live_count == 0);
            }
        };

        class CaseDetachCompletedTask : public TestCase {
        public:
            CaseDetachCompletedTask()
                : TestCase("detach 已完成任务立即释放帧") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                FrameProbe::reset();
                {
                    auto task = make_void_task();
                    ttest(task.done());
                    task.detach();
                    ttest(!task.valid());
                    ttest(task.done());
                }
                ttest(FrameProbe::live_count == 0);
                ttest(FrameProbe::destruct_count == 1);
            }
        };

        class CaseNormalContinuation : public TestCase {
        public:
            CaseNormalContinuation()
                : TestCase(
                      "普通 co_await 路径恢复 continuation 并保留销毁责任") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                FrameProbe::reset();
                bool child_ran = false;
                ManualSuspendGate gate;
                {
                    auto task = await_normal_child(gate, child_ran);
                    ttest(!task.done());
                    ttest(gate.suspended != nullptr);
                    ttest(!child_ran);
                    gate.suspended.resume();
                    ttest(task.done());
                    ttest(child_ran);
                    ttest(task.result() == 16);
                }
                ttest(FrameProbe::live_count == 0);
            }
        };

        class CaseWaitCotaskPropagateReason : public TestCase {
        public:
            CaseWaitCotaskPropagateReason()
                : TestCase("wait::cotask 向父协程传播 wait_reason") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                constexpr size_t WAIT_REASON = 17;
                auto task                          = wait_parent(WAIT_REASON);
                ttest(task.valid());
                ttest(!task.done());
                ttest(task.wait_context().pending());
                ttest(task.wait_context().wait_reason == WAIT_REASON);
                ttest(task.wait_context().suspended_leaf != nullptr);
            }
        };

        class CaseWaitCotaskClearOnNormalAwait : public TestCase {
        public:
            CaseWaitCotaskClearOnNormalAwait()
                : TestCase(
                      "wait::cotask await 普通 awaitable 时清空等待上下文") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                auto task = wait_after_normal_suspend();
                ttest(task.valid());
                ttest(task.done());
                ttest(!task.wait_context().pending());
                ttest(task.wait_context().wait_reason == 0);
                ttest(task.result() == 5);
            }
        };

        class CaseAwaitReadyFuture : public TestCase {
        public:
            CaseAwaitReadyFuture()
                : TestCase("co_await Future<T> 返回 Result<T>") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                auto task = co_await_ready_future();
                ttest(task.done());
                auto result = task.result();
                ttest(result.has_value());
                ttest(result.value() == 31);
            }
        };

        class CaseAwaitResultFutureFlatten : public TestCase {
        public:
            CaseAwaitResultFutureFlatten()
                : TestCase("co_await Future<Result<T>> 扁平化为 Result<T>") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                auto task = co_await_result_future();
                ttest(task.done());
                auto result = task.result();
                ttest(result.has_value());
                ttest(result.value() == 47);
            }
        };
    }  // namespace

    void collect_tests(TestFramework& framework) {
        auto coroutine_cases = util::ArrayList<TestCase*>();
        coroutine_cases.push_back(new CaseScopedVoidTask());
        coroutine_cases.push_back(new CaseScopedValueTask());
        coroutine_cases.push_back(new CaseDetachCompletedTask());
        coroutine_cases.push_back(new CaseNormalContinuation());
        coroutine_cases.push_back(new CaseWaitCotaskPropagateReason());
        coroutine_cases.push_back(new CaseWaitCotaskClearOnNormalAwait());
        framework.add_category(
            new TestCategory("coroutine", std::move(coroutine_cases)));

        auto future_cases = util::ArrayList<TestCase*>();
        future_cases.push_back(new CaseAwaitReadyFuture());
        future_cases.push_back(new CaseAwaitResultFutureFlatten());
        framework.add_category(
            new TestCategory("future", std::move(future_cases)));
    }

}  // namespace test::coroutine
