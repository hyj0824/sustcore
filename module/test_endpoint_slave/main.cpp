#include <sustcore/bootstrap.h>
#include <kmod/syscall.h>
#include <sustcore/capability.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr uint64_t kValueK    = 0xfedcba9876543210ULL;
constexpr size_t kRepeatCount = 10;
constexpr uint32_t kBootstrapTypeEndpoint = 0xFFFF0001U;

static CapIdx bootstrap_endpoint() {
    CapIdx endpoint = cap::null;
    if (!bootstrap_find_single_cap(__bsargv, __bsargc, kBootstrapTypeEndpoint,
                                   endpoint))
    {
        printf("test-endpoint-slave: 启动参数无效 bsargc=%u\n", __bsargc);
        exit(-1);
    }
    return endpoint;
}

static uint64_t recv_u64(CapIdx endpoint, const char *tag) {
    uint64_t value = 0;
    MsgPacket packet{
        .msgsz = sizeof(value),
        .capsz = 0,
    };

    (void)sys_endpoint_recv(endpoint, &packet).to_result();
    if (packet.msgsz != sizeof(value) || packet.capsz != 0) {
        printf("%s: 无效的消息 msgsz=%u capsz=%u\n", tag, packet.msgsz,
               packet.capsz);
        exit(-1);
    }

    memcpy(&value, packet.msgbuf, sizeof(value));
    return value;
}

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;
    printf("test-endpoint-slave 启动! \n");
    printf("test-endpoint-slave: pid=%u\n", sys_getpid(__pcb_cap).value());

    CapIdx endpoint_cap = bootstrap_endpoint();

    printf("test-endpoint-slave: 发送密钥 K=0x%016lx\n", kValueK);
    uint64_t value = kValueK;
    MsgPacket packet{
        .msgsz = sizeof(kValueK),
        .capsz = 0,
    };
    memcpy(packet.msgbuf, &value, sizeof(value));
    (void)sys_endpoint_send(endpoint_cap, &packet).to_result();

    for (size_t round = 0; round < kRepeatCount; ++round) {
        uint64_t c = recv_u64(endpoint_cap, "test-endpoint-slave");
        uint64_t v = c ^ kValueK;
        printf("test-endpoint-slave: 第%u轮 K=0x%016lx C=0x%016lx V=0x%016lx\n",
               round, kValueK, c, v);
    }

    exit(-1);
    return 0;
}
