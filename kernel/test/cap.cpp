/**
 * @file cap.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 能力系统测试实现
 * @version alpha-1.0.0
 * @date 2026-03-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cap/capability.h>
#include <cap/cholder.h>
#include <guard.h>
#include <object/endpoint.h>
#include <object/intobj.h>
#include <object/perm.h>
#include <test/cap.h>

namespace test::cap {
    namespace kcap = ::cap;

    struct CountingPayload : public kcap::_PayloadHelper<PayloadType::INTOBJ> {
        static size_t destruct_count;
        int value;

        explicit CountingPayload(int v) : value(v) {}

        void destruct() override {
            destruct_count++;
            delete this;
        }
    };

    size_t CountingPayload::destruct_count = 0;

    static Result<kcap::CHolder *> new_holder() {
        auto &chman     = kcap::CHolderManager::inst();
        auto holder_res = chman.create_holder();
        propagate(holder_res);
        return holder_res.value();
    }

    class CaseCreateObject : public TestCase {
    public:
        CaseCreateObject() : TestCase("创建对象能力并验证读写") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            auto holder_res = new_holder();
            tassert(holder_res.has_value(), "创建 CHolder");
            auto *holder = holder_res.value();

            auto create_res = holder->create<kcap::IntPayload>(12345);
            tassert(create_res.has_value(), "创建 IntPayload 能力");
            CapIdx idx = create_res.value();

            auto cap_res = holder->lookup(idx);
            tassert(cap_res.has_value(), "取回能力");
            kcap::IntObj op(util::nnullforce(cap_res.value()));

            auto read_res = op.read();
            tassert(read_res.has_value() && read_res.value() == 12345,
                    "初始读值正确");

            auto write_res = op.write(54321);
            tassert(write_res.has_value(), "写入成功");
            auto read_after_write = op.read();
            tassert(read_after_write.has_value() &&
                        read_after_write.value() == 54321,
                    "写入后读值正确");
        }
    };

    class CaseSharedPayloadClone : public TestCase {
    public:
        CaseSharedPayloadClone() : TestCase("clone 默认共享 payload") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            auto holder_res = new_holder();
            tassert(holder_res.has_value(), "创建 CHolder");
            auto *holder = holder_res.value();

            auto create_res = holder->create<kcap::IntPayload>(7);
            tassert(create_res.has_value(), "创建源能力");
            CapIdx src = create_res.value();

            auto clone_res = holder->clone(src);
            tassert(clone_res.has_value(), "clone 成功");
            CapIdx dst = clone_res.value();

            kcap::IntObj src_op(
                util::nnullforce(holder->lookup(src).value()));
            kcap::IntObj dst_op(
                util::nnullforce(holder->lookup(dst).value()));

            auto write_res = src_op.write(9);
            tassert(write_res.has_value(), "源能力写入成功");
            auto cloned_read = dst_op.read();
            tassert(cloned_read.has_value() && cloned_read.value() == 9,
                    "clone 能力观察到同一 payload");
        }
    };

    class CaseDowngradeAndDerive : public TestCase {
    public:
        CaseDowngradeAndDerive() : TestCase("downgrade 与 derive 权限降级") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            auto holder_res = new_holder();
            tassert(holder_res.has_value(), "创建 CHolder");
            auto *holder = holder_res.value();

            auto create_res = holder->create<kcap::IntPayload>(100);
            tassert(create_res.has_value(), "创建源能力");
            CapIdx src = create_res.value();

            b64 read_only   = perm::intobj::READ;
            auto derive_res = holder->derive(src, read_only);
            tassert(derive_res.has_value(), "derive READ-only 成功");
            CapIdx dst = derive_res.value();

            kcap::IntObj derived(
                util::nnullforce(holder->lookup(dst).value()));
            auto read_res = derived.read();
            tassert(read_res.has_value() && read_res.value() == 100,
                    "派生能力可读");

            auto write_res = derived.write(200);
            tassert(!write_res.has_value() &&
                        write_res.error() == ErrCode::INSUFFICIENT_PERMISSIONS,
                    "派生能力不可写");
        }
    };

    class CasePayloadDestruct : public TestCase {
    public:
        CasePayloadDestruct() : TestCase("payload 最后引用释放时 destruct") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            CountingPayload::destruct_count = 0;

            auto holder_res = new_holder();
            tassert(holder_res.has_value(), "创建 CHolder");
            auto *holder = holder_res.value();

            auto create_res = holder->create<CountingPayload>(1);
            tassert(create_res.has_value(), "创建计数 payload");
            CapIdx src = create_res.value();

            auto clone_res = holder->clone(src);
            tassert(clone_res.has_value(), "clone 成功");
            CapIdx dst = clone_res.value();

            auto remove_src = holder->remove(src);
            tassert(remove_src.has_value(), "删除源能力");
            ttest(CountingPayload::destruct_count == 0);

            auto remove_dst = holder->remove(dst);
            tassert(remove_dst.has_value(), "删除最后一个能力");
            ttest(CountingPayload::destruct_count == 1);
        }
    };

    class CaseEndpointTransferPermissions : public TestCase {
    public:
        CaseEndpointTransferPermissions()
            : TestCase("Endpoint传递cap检查MIGRATE权限") {}

        void _run(void *env [[maybe_unused]]) const noexcept override {
            auto source_res = new_holder();
            auto dest_res   = new_holder();
            tassert(source_res.has_value() && dest_res.has_value(),
                    "创建传递双方 CHolder");
            auto *source = source_res.value();
            auto *dest   = dest_res.value();

            auto once_res = source->insert_to_free(
                new kcap::IntPayload(42),
                perm::basic::MIGRATE_ONCE | perm::intobj::READ);
            tassert(once_res.has_value(), "创建 MIGRATE_ONCE 源能力");

            auto transfer_res =
                source->transfer_to(*dest, once_res.value());
            tassert(transfer_res.has_value(), "MIGRATE_ONCE允许跨holder传递");

            auto old_lookup = source->lookup(once_res.value());
            tassert(!old_lookup.has_value(), "MIGRATE_ONCE传递后源slot被消费");

            auto moved_cap_res = dest->lookup(transfer_res.value());
            tassert(moved_cap_res.has_value(), "目标holder收到cap");
            tassert(!moved_cap_res.value()->imply(perm::basic::MIGRATE_ONCE),
                    "目标cap清除MIGRATE_ONCE");

            kcap::IntObj int_obj(util::nnullforce(moved_cap_res.value()));
            auto read_res = int_obj.read();
            tassert(read_res.has_value() && read_res.value() == 42,
                    "接收方cap保留对象权限");

            auto plain_res = source->insert_to_free(
                new kcap::IntPayload(7), perm::intobj::READ);
            tassert(plain_res.has_value(), "创建无传递权限能力");
            auto denied_res =
                source->transfer_to(*dest, plain_res.value());
            tassert(!denied_res.has_value() &&
                        denied_res.error() == ErrCode::INSUFFICIENT_PERMISSIONS,
                    "无CLONE/MIGRATE权限时拒绝传递");
        }
    };

    void collect_tests(TestFramework &framework) {
        auto cases = util::ArrayList<TestCase *>();
        cases.push_back(new CaseCreateObject());
        cases.push_back(new CaseSharedPayloadClone());
        cases.push_back(new CaseDowngradeAndDerive());
        cases.push_back(new CasePayloadDestruct());
        cases.push_back(new CaseEndpointTransferPermissions());

        framework.add_category(
            new TestCategory("capability", std::move(cases)));
    }
}  // namespace test::cap
