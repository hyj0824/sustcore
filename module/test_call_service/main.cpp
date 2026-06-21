#include <kmod/bootstrap.h>
#include <kmod/syscall.h>
#include <sustcore/capability.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr uint64_t kOpEncrypt     = 1;
constexpr uint64_t kOpDecrypt     = 2;
constexpr uint32_t kBootstrapTypeEndpoint = 0xFFFF0001U;

struct CallRequest {
    uint64_t op;
    uint64_t key;
    uint64_t value;
};

static void serve_one(CapIdx endpoint) {
    CallRequest request{};
    MsgPacket packet{
        .msgsz = sizeof(request),
        .capsz = MAX_MSG_CAPS,
    };

    sys_endpoint_recv(endpoint, &packet);
    if (packet.msgsz != sizeof(request) || packet.capsz != 1) {
        printf("test_call_service: 请求格式错误 msgsz=%u capsz=%u\n",
               packet.msgsz, packet.capsz);
        exit(-1);
    }
    memcpy(&request, packet.msgbuf, sizeof(request));

    CapInfo reply_info{};
    if (!sys_cap_lookup(packet.caplist[0], &reply_info) ||
        reply_info.type != PayloadType::REPLY)
    {
        printf("test_call_service: 未收到reply capability\n");
        exit(-1);
    }

    uint64_t result = 0;
    if (request.op == kOpEncrypt) {
        result = request.key ^ request.value;
        printf("test_call_service: encrypt K=0x%016lx V=0x%016lx C=0x%016lx\n",
               request.key, request.value, result);
    } else if (request.op == kOpDecrypt) {
        result = request.key ^ request.value;
        printf("test_call_service: decrypt K=0x%016lx C=0x%016lx V=0x%016lx\n",
               request.key, request.value, result);
    } else {
        printf("test_call_service: 未知操作=%u\n", request.op);
        exit(-1);
    }

    MsgPacket reply{
        .msgsz = sizeof(result),
        .capsz = 0,
    };
    memcpy(reply.msgbuf, &result, sizeof(result));
    endpoint_reply(packet.caplist[0], &reply);
}

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv_in[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv_in;
    printf("test_call_service: start pid=%u\n", sys_getpid(__pcb_cap));
    CapIdx endpoint = sys_endpoint_create();
    if (endpoint == cap::error) {
        printf("test_call_service: 创建endpoint失败\n");
        exit(-1);
    }

    CapIdx initial_caps[] = {endpoint, cap::null};
    BootstrapSingleCapRecord<kBootstrapTypeEndpoint> bootstrap(endpoint);
    const char *bsargv[] = {reinterpret_cast<const char *>(&bootstrap),
                            nullptr};
    int fd                = kmod_fopen("/initrd/test_call_user.mod", "x");
    CapIdx user_pcb       =
        fd < 0 ? cap::error
               : sys_create_process(kmod_getcap(fd), SCHED_CLASS_RR,
                                    initial_caps, nullptr, nullptr, bsargv);
    if (fd >= 0) {
        kmod_fclose(fd);
    }
    if (user_pcb == cap::error) {
        printf("test_call_service: 创建user失败\n");
        exit(-1);
    }
    sys_cap_remove(user_pcb);

    serve_one(endpoint);
    serve_one(endpoint);

    printf("test_call_service: done\n");
    exit(0);
    return 0;
}
