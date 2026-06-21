#include <kmod/bootstrap.h>
#include <kmod/syscall.h>
#include <sustcore/capability.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr uint64_t kValueV    = 0x123456789abcdef0ULL;
constexpr size_t kRepeatCount = 10;
constexpr uint32_t kBootstrapTypeEndpoint = 0xFFFF0001U;

static uint64_t recv_u64(CapIdx endpoint, const char *tag) {
    uint64_t value = 0;
    MsgPacket packet{
        .msgsz = sizeof(value),
        .capsz = 0,
    };

    sys_endpoint_recv(endpoint, &packet);
    if (packet.msgsz != sizeof(value) || packet.capsz != 0) {
        printf("%s: 无效的消息 msgsz=%u capsz=%u\n", tag, packet.msgsz,
               packet.capsz);
        exit(-1);
    }

    memcpy(&value, packet.msgbuf, sizeof(value));
    return value;
}

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv_in[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv_in;
    printf("test-endpoint-master: start\n");
    printf("test-endpoint-master: pid=%u\n", sys_getpid(__pcb_cap));

    CapIdx endpoint = sys_endpoint_create();
    if (endpoint == cap::error) {
        printf("test-endpoint-master: 创建端点失败!\n");
        exit(-1);
    }

    CapIdx initial_caps[] = {endpoint, cap::null};
    BootstrapSingleCapRecord<kBootstrapTypeEndpoint> bootstrap(endpoint);
    const char *bsargv[] = {reinterpret_cast<const char *>(&bootstrap),
                            nullptr};
    int fd = kmod_fopen("/initrd/test_endpoint_slave.mod", "x");
    CapIdx slave_pcb =
        fd < 0 ? cap::error
               : sys_create_process(kmod_getcap(fd), SCHED_CLASS_RR,
                                    initial_caps, nullptr, nullptr, bsargv);
    if (fd >= 0) {
        kmod_fclose(fd);
    }
    if (slave_pcb == cap::error) {
        printf("test-endpoint-master: 创建 test-endpoint-slave 失败!\n");
        exit(-1);
    }

    uint64_t k = recv_u64(endpoint, "test-endpoint-master");
    printf("test-endpoint-master: 收到密钥 K=0x%016lx\n", k);

    for (size_t round = 0; round < kRepeatCount; ++round) {
        uint64_t v   = kValueV + round;
        uint64_t c   = k ^ v;
        MsgPacket packet{
            .msgsz = sizeof(c),
            .capsz = 0,
        };
        memcpy(packet.msgbuf, &c, sizeof(c));
        printf("test-endpoint-master: 第%u轮 V=0x%016lx C=0x%016lx\n", round, v,
               c);
        sys_endpoint_send(endpoint, &packet);
    }

    exit(-1);
    return 0;
}
