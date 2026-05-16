/**
 * @file functional.cpp
 * @author
 * @brief functional: invoke / mem_fn 测试
 */

#include <test/functional.h>

#include <functional>
#include <string>

namespace test::functional {

    struct Sample {
        int factor = 2;

        int mul(int x) {
            return factor * x;
        }

        int cmul(int x) const {
            return factor * x;
        }
    };

    class CaseInvokeFunction : public TestCase {
    public:
        CaseInvokeFunction() : TestCase("invoke 普通函数与可调用对象") {}

        static int add(int a, int b) {
            return a + b;
        }

        void _run(void* env [[maybe_unused]]) const noexcept override {
            ttest(std::invoke(add, 1, 2) == 3);

            auto lambda = [](int a, int b) { return a * b; };
            ttest(std::invoke(lambda, 3, 4) == 12);
        }
    };

    class CaseInvokeMember : public TestCase {
    public:
        CaseInvokeMember() : TestCase("invoke 成员函数与成员变量") {}

        void _run(void* env [[maybe_unused]]) const noexcept override {
            Sample s{3};
            const Sample cs{4};

            ttest(std::invoke(&Sample::mul, &s, 5) == 15);
            ttest(std::invoke(&Sample::mul, s, 5) == 15);

            ttest(std::invoke(&Sample::cmul, &cs, 5) == 20);
            ttest(std::invoke(&Sample::cmul, cs, 5) == 20);

            auto pm = &Sample::factor;
            ttest(std::invoke(pm, &s) == 3);
            ttest(std::invoke(pm, s) == 3);
        }
    };

    class CaseMemFn : public TestCase {
    public:
        CaseMemFn() : TestCase("mem_fn 包装成员指针") {}

        void _run(void* env [[maybe_unused]]) const noexcept override {
            Sample s{5};

            auto f_mul = std::mem_fn(&Sample::mul);
            ttest(f_mul(&s, 2) == 10);
            ttest(f_mul(s, 3) == 15);

            auto f_factor = std::mem_fn(&Sample::factor);
            ttest(f_factor(&s) == 5);
            ttest(f_factor(s) == 5);

            const Sample cs{6};
            auto f_cmul = std::mem_fn(&Sample::cmul);
            ttest(f_cmul(&cs, 2) == 12);
            ttest(f_cmul(cs, 3) == 18);
        }
    };

    class CaseFunctionObject : public TestCase {
    public:
        CaseFunctionObject() : TestCase("function 小对象、复制与移动") {}

        struct BigCapture {
            int values[8];

            int operator()(int x) {
                return values[0] + values[7] + x;
            }
        };

        void _run(void* env [[maybe_unused]]) const noexcept override {
            int base = 7;
            std::function<int(int)> small = [base](int x) {
                return base + x;
            };
            ttest(static_cast<bool>(small));
            ttest(small(5) == 12);

            std::function<int(int)> copied = small;
            ttest(static_cast<bool>(copied));
            ttest(copied(6) == 13);
            ttest(small(1) == 8);

            std::function<int(int)> moved = std::move(copied);
            ttest(static_cast<bool>(moved));
            ttest(!static_cast<bool>(copied));
            ttest(moved(2) == 9);

            BigCapture big{{1, 2, 3, 4, 5, 6, 7, 8}};
            std::function<int(int)> heap_backed = big;
            ttest(static_cast<bool>(heap_backed));
            ttest(heap_backed(10) == 19);

            std::function<void(int&)> void_func = [](int& value) {
                value += 3;
            };
            int value = 4;
            void_func(value);
            ttest(value == 7);

            std::function<int(int)> empty;
            ttest(!static_cast<bool>(empty));
        }
    };

    void collect_tests(TestFramework& framework) {
        auto cases = util::ArrayList<TestCase*>();
        cases.push_back(new CaseInvokeFunction());
        cases.push_back(new CaseInvokeMember());
        cases.push_back(new CaseMemFn());
        cases.push_back(new CaseFunctionObject());

        framework.add_category(new TestCategory("functional", std::move(cases)));
    }

}  // namespace test::functional
