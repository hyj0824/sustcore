/**
 * @file coroutine.cpp
 * @brief util::cotask lifecycle tests
 */

#include <sus/coroutine.h>
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

            FrameProbe(const FrameProbe&) = delete;
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

        util::cotask<int> detached_child(bool& resumed_flag) {
            FrameProbe probe;
            (void)probe;
            resumed_flag = true;
            co_return 7;
        }

        util::cotask<int> await_detached_child(bool& child_ran) {
            auto child = detached_child(child_ran);
            child.detach();
            auto value = co_await child;
            co_return value + 5;
        }

        util::cotask<int> await_normal_child(bool& child_ran) {
            auto child = detached_child(child_ran);
            auto value = co_await child;
            co_return value + 9;
        }

        class CaseScopedVoidTask : public TestCase {
        public:
            CaseScopedVoidTask() : TestCase("cotask<void> 作用域析构回收完成帧") {}

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
            CaseScopedValueTask() : TestCase("cotask<T> 保留结果并由析构释放帧") {}

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
            CaseDetachCompletedTask() : TestCase("detach 已完成任务立即释放帧") {}

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

        class CaseDetachedContinuation : public TestCase {
        public:
            CaseDetachedContinuation()
                : TestCase("detached 协程存在 continuation 时不丢失恢复") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                FrameProbe::reset();
                bool child_ran = false;
                {
                    auto task = await_detached_child(child_ran);
                    ttest(task.done());
                    ttest(child_ran);
                    ttest(task.result() == 12);
                }
                ttest(FrameProbe::live_count == 0);
                ttest(FrameProbe::destruct_count == 2);
            }
        };

        class CaseNormalContinuation : public TestCase {
        public:
            CaseNormalContinuation()
                : TestCase("普通 co_await 路径恢复 continuation 并保留销毁责任") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                FrameProbe::reset();
                bool child_ran = false;
                {
                    auto task = await_normal_child(child_ran);
                    ttest(task.done());
                    ttest(child_ran);
                    ttest(task.result() == 16);
                }
                ttest(FrameProbe::live_count == 0);
                ttest(FrameProbe::destruct_count == 2);
            }
        };

        class CaseAwaitCompletedDetachedTask : public TestCase {
        public:
            CaseAwaitCompletedDetachedTask()
                : TestCase("已完成 detached task 可同步 await_resume 并释放帧") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                FrameProbe::reset();
                {
                    auto task = make_value_task(23);
                    task.detach();
                    ttest(task.done());
                    auto value = task.operator co_await().await_resume();
                    ttest(value.value == 23);
                    ttest(!task.valid());
                    ttest(task.done());
                }
                ttest(FrameProbe::live_count == 0);
                ttest(FrameProbe::destruct_count == 1);
            }
        };
    }  // namespace

    void collect_tests(TestFramework& framework) {
        auto cases = util::ArrayList<TestCase*>();
        cases.push_back(new CaseScopedVoidTask());
        cases.push_back(new CaseScopedValueTask());
        cases.push_back(new CaseDetachCompletedTask());
        cases.push_back(new CaseDetachedContinuation());
        cases.push_back(new CaseNormalContinuation());
        cases.push_back(new CaseAwaitCompletedDetachedTask());

        framework.add_category(new TestCategory("coroutine", std::move(cases)));
    }

}  // namespace test::coroutine
