/**
 * @file array.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief array 测试
 * @version alpha-1.0.0
 * @date 2026-05-19
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <test/array.h>

#include <array>
#include <nt/errors.h>
#include <type_traits>
#include <utility>

namespace test::array {
    constexpr bool constexpr_array_smoke() {
        std::array<int, 3> a{1, 2, 3};
        a.fill(4);
        std::array<int, 3> b{7, 8, 9};
        a.swap(b);
        return a.front() == 7 && a.back() == 9 && b[1] == 4 &&
               std::get<2>(a) == 9;
    }

    static_assert(constexpr_array_smoke());

    class CaseBasicAccess : public TestCase {
    public:
        CaseBasicAccess() : TestCase("基础访问") {}
        void _run(void* env [[maybe_unused]]) const noexcept override {
            std::array<int, 3> a{1, 2, 3};

            ttest(!a.empty());
            ttest(a.size() == 3);
            ttest(a.max_size() == 3);
            ttest(a.data() == a.begin());
            ttest(a.end() == a.begin() + 3);
            ttest(a[0] == 1);
            ttest(a.front() == 1);
            ttest(a.back() == 3);

            a[1] = 9;
            ttest(a.at_nt(1).has_value());
            ttest(*a.at_nt(1).value() == 9);

            const std::array<int, 3>& ca = a;
            auto ok = ca.at_nt(2);
            ttest(ok.has_value());
            ttest(*ok.value() == 3);

            auto fail = a.at_nt(3);
            ttest(!fail);
            ttest(fail.error() == std::error_type::OUT_OF_RANGE);
        }
    };

    class CaseIteratorAndFill : public TestCase {
    public:
        CaseIteratorAndFill() : TestCase("迭代与填充") {}
        void _run(void* env [[maybe_unused]]) const noexcept override {
            std::array<int, 4> a{1, 2, 3, 4};

            int sum = 0;
            for (int value : a) {
                sum += value;
            }
            ttest(sum == 10);
            ttest(a.cbegin() == a.begin());
            ttest(a.cend() == a.end());

            a.fill(7);
            ttest(a[0] == 7);
            ttest(a[1] == 7);
            ttest(a[2] == 7);
            ttest(a[3] == 7);
        }
    };

    class CaseSwapAndCompare : public TestCase {
    public:
        CaseSwapAndCompare() : TestCase("交换与比较") {}
        void _run(void* env [[maybe_unused]]) const noexcept override {
            std::array<int, 3> a{1, 2, 3};
            std::array<int, 3> b{1, 2, 4};
            std::array<int, 3> c{9, 8, 7};

            ttest(a == a);
            ttest(a != b);
            ttest(a < b);
            ttest(b > a);
            ttest(a <= b);
            ttest(b >= a);

            a.swap(c);
            ttest(a[0] == 9);
            ttest(c[0] == 1);

            std::swap(a, c);
            ttest(a[0] == 1);
            ttest(c[0] == 9);
        }
    };

    class CaseTupleInterface : public TestCase {
    public:
        CaseTupleInterface() : TestCase("tuple 接口") {}
        void _run(void* env [[maybe_unused]]) const noexcept override {
            std::array<int, 3> a{4, 5, 6};
            const std::array<int, 3>& ca = a;

            ttest(std::tuple_size<decltype(a)>::value == 3);
            ttest((std::is_same_v<
                   typename std::tuple_element<1, decltype(a)>::type, int>));
            ttest(std::get<0>(a) == 4);
            ttest(std::get<1>(ca) == 5);

            std::get<2>(a) = 9;
            ttest(a[2] == 9);
        }
    };

    class CaseDeductionAndEmpty : public TestCase {
    public:
        CaseDeductionAndEmpty() : TestCase("推导与空数组") {}
        void _run(void* env [[maybe_unused]]) const noexcept override {
            std::array deduced{1, 2, 3};
            ttest((std::is_same_v<decltype(deduced), std::array<int, 3>>));
            ttest(deduced[2] == 3);

            std::array<int, 0> empty{};
            ttest(empty.empty());
            ttest(empty.size() == 0);
            ttest(empty.begin() == empty.end());
            ttest(empty.data() == nullptr);

            auto fail = empty.at_nt(0);
            ttest(!fail);
            ttest(fail.error() == std::error_type::OUT_OF_RANGE);
        }
    };

    void collect_tests(TestFramework& framework) {
        auto cases = util::ArrayList<TestCase*>();
        cases.push_back(new CaseBasicAccess());
        cases.push_back(new CaseIteratorAndFill());
        cases.push_back(new CaseSwapAndCompare());
        cases.push_back(new CaseTupleInterface());
        cases.push_back(new CaseDeductionAndEmpty());

        framework.add_category(new TestCategory("array", std::move(cases)));
    }
}  // namespace test::array
