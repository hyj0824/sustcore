/**
 * @file wait.cpp
 * @brief wait 子系统测试
 */

#include <task/wait.h>
#include <test/wait.h>

namespace test::wait {
    namespace {
        class CaseWaitEventRejectsInvalidReason : public TestCase {
        public:
            CaseWaitEventRejectsInvalidReason()
                : TestCase("wait_event 拒绝无效 wait_reason") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                auto res = task::wait::wait_event(0, []() { return true; });
                ttest(!res.has_value());
                ttest(res.error() == ErrCode::INVALID_PARAM);
            }
        };

        class CaseWaitEventRejectsEmptyPredicate : public TestCase {
        public:
            CaseWaitEventRejectsEmptyPredicate()
                : TestCase("wait_event 拒绝空 ready_predicate") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                auto res = task::wait::wait_event(1, {});
                ttest(!res.has_value());
                ttest(res.error() == ErrCode::INVALID_PARAM);
            }
        };

        class CaseWaitEventReturnsImmediatelyWhenReady : public TestCase {
        public:
            CaseWaitEventReturnsImmediatelyWhenReady()
                : TestCase("wait_event 在事件已就绪时立即返回") {}

            void _run(void* env [[maybe_unused]]) const noexcept override {
                bool checked = false;
                auto res     = task::wait::wait_event(1, [&checked]() {
                    checked = true;
                    return true;
                });
                ttest(res.has_value());
                ttest(checked);
            }
        };

        class CaseFutureValueRejectsPending : public TestCase {
        public:
            CaseFutureValueRejectsPending()
                : TestCase("Future::value 在 pending 时返回 FUTURE_PENDING") {}

            void _run(void *env [[maybe_unused]]) const noexcept override {
                task::wait::Promise<int> promise;
                auto future = promise.future();
                auto value_res = future.value();
                ttest(!value_res.has_value());
                ttest(value_res.error() == ErrCode::FUTURE_PENDING);
            }
        };

        class CaseFutureCancelTransitionsState : public TestCase {
        public:
            CaseFutureCancelTransitionsState()
                : TestCase("Future::cancle 将 pending Future 置为 cancled") {}

            void _run(void *env [[maybe_unused]]) const noexcept override {
                task::wait::Promise<int> promise;
                auto future = promise.future();
                auto cancel_res = future.cancle();
                ttest(cancel_res.has_value());

                auto value_res = future.value();
                ttest(!value_res.has_value());
                ttest(value_res.error() == ErrCode::FUTURE_CANCLED);
            }
        };

        class CaseFutureValueConsumesResult : public TestCase {
        public:
            CaseFutureValueConsumesResult()
                : TestCase("Future::value 读取成功后转为 consumed") {}

            void _run(void *env [[maybe_unused]]) const noexcept override {
                task::wait::Promise<int> promise;
                auto future = promise.future();
                auto set_res = promise.set_value(42);
                ttest(set_res.has_value());

                auto first_res = future.value();
                ttest(first_res.has_value());
                ttest(first_res.value() == 42);

                auto second_res = future.value();
                ttest(!second_res.has_value());
                ttest(second_res.error() == ErrCode::FUTURE_CONSUMED);
            }
        };

        class CaseFutureFlattenResultValue : public TestCase {
        public:
            CaseFutureFlattenResultValue()
                : TestCase("Future<Result<T>>::value 扁平化结果") {}

            void _run(void *env [[maybe_unused]]) const noexcept override {
                PromiseResult<int> promise;
                auto future = promise.future();
                auto set_res = promise.set_value(Result<int>{77});
                ttest(set_res.has_value());

                auto value_res = future.value();
                ttest(value_res.has_value());
                ttest(value_res.value() == 77);
            }
        };
    }  // namespace

    void collect_tests(TestFramework& framework) {
        auto cases = util::ArrayList<TestCase*>();
        cases.push_back(new CaseWaitEventRejectsInvalidReason());
        cases.push_back(new CaseWaitEventRejectsEmptyPredicate());
        cases.push_back(new CaseWaitEventReturnsImmediatelyWhenReady());
        cases.push_back(new CaseFutureValueRejectsPending());
        cases.push_back(new CaseFutureCancelTransitionsState());
        cases.push_back(new CaseFutureValueConsumesResult());
        cases.push_back(new CaseFutureFlattenResultValue());

        framework.add_category(new TestCategory("wait", std::move(cases)));
    }
}  // namespace test::wait
