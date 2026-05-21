#include <kmod/syscall.h>
#include <rpc/metahelper.h>
#include <rpc/packet.h>
#include <sustcore/capability.h>

#include <cstddef>
#include <cstdio>
#include <string>

constexpr sus_i32 kTestValue    = 114514;
constexpr size_t kScanSlots     = 16;

class SampleServiceInterface {
public:
    [[=rpc::service_name]]
    constexpr static const char *SERVICE_NAME = "sample_service";

    [[=rpc::service_magic]]
    constexpr static sus_u32 SERVICE_MAGIC = 0x12345678;

    [[=rpc::expose(0)]]
    virtual void set(sus_i32 value) = 0;

    [[=rpc::expose(1)]]
    virtual sus_i32 get() = 0;
};

class SampleClient : public rpc::MetaClient<SampleServiceInterface> {
public:
    explicit SampleClient(CapIdx endpoint)
        : rpc::MetaClient<SampleServiceInterface>(endpoint) {}

    Result<void> set(sus_i32 value) {
        return call<^^SampleServiceInterface::set>(value);
    }

    Result<sus_i32> get() {
        return call<^^SampleServiceInterface::get>();
    }

    Result<void> close() {
        auto session_res = start();
        propagate(session_res);
        return session_res.value().get().close();
    }
};

static CapIdx find_unique_endpoint_cap() {
    CapIdx found = cap::null;
    size_t count = 0;

    for (size_t slot = 0; slot < kScanSlots; ++slot) {
        CapIdx candidate = cap::make(0, slot);
        CapInfo info{};
        if (!sys_cap_lookup(candidate, &info) ||
            info.type != PayloadType::ENDPOINT)
        {
            continue;
        }

        found = candidate;
        ++count;
    }

    if (count != 1) {
        printf("test_rpc_client: 预期找到一个endpoint, 实际=%u\n", count);
        exit(-1);
    }

    return found;
}

static void fail(const char *msg) {
    printf("test_rpc_client: %s\n", msg);
    exit(-1);
}

int kmod_main() {
    printf("test_rpc_client: start pid=%u\n", sys_getpid(__pcb_cap));
    CapIdx endpoint = find_unique_endpoint_cap();

    SampleClient client(endpoint);

    auto set_res = client.set(kTestValue);
    if (!set_res.has_value()) {
        fail("RPC set(i32)调用失败");
    }
    printf("test_rpc_client: set(%d) ok\n", kTestValue);

    auto get_res = client.get();
    if (!get_res.has_value()) {
        fail("RPC get()调用失败");
    }

    sus_i32 value = get_res.value();
    printf("test_rpc_client: get()=%d expected=%d\n", value, kTestValue);
    if (value != kTestValue) {
        fail("RPC get()返回值不匹配");
    }

    auto close_res = client.close();
    if (!close_res.has_value()) {
        fail("关闭RPC session失败");
    }

    printf("test_rpc_client: RPC测试完成\n");
    exit(-1);
    return 0;
}
