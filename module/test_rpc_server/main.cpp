#include <sustcore/bootstrap.h>
#include <kmod/syscall.h>
#include <rpc/metahelper.h>
#include <rpc/packet.h>
#include <sustcore/msg.h>

#include <cstdio>
#include <string>

class SampleServiceInterface {
public:
    [[= rpc::service_name]] constexpr static const char *SERVICE_NAME =
        "sample_service";

    [[= rpc::service_magic]] constexpr static sus_u32 SERVICE_MAGIC =
        0x12345678;

    [[= rpc::expose(0)]] virtual void set(sus_i32 value) = 0;

    [[= rpc::expose(1)]] virtual sus_i32 get() = 0;
};

constexpr uint32_t kBootstrapTypeEndpoint = 0xFFFF0001U;

class SampleServer
    : public SampleServiceInterface,
      public rpc::MetaServer<SampleServiceInterface, SampleServer> {
private:
    sus_i32 _x = 0;
    using MetaServer = rpc::MetaServer<SampleServiceInterface, SampleServer>;
public:
    explicit SampleServer(CapIdx endpoint)
        : MetaServer(endpoint) {}

    void set(sus_i32 value) override {
        _x = value;
    }

    sus_i32 get() override {
        return _x;
    }
};

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv_in[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv_in;
    auto endpoint_res = sys_endpoint_create().to_result();
    CapIdx endpoint =
        endpoint_res.has_value() ? endpoint_res.value() : cap::error;
    if (endpoint == cap::error) {
        printf("Failed to create endpoint\n");
        exit(0);
    }

    SampleServer server(endpoint);
    printf("Server is running endpoint=%p\n", (void *)endpoint);

    CapIdx initial_caps[] = {endpoint, cap::null};
    BootstrapSingleCapRecord<kBootstrapTypeEndpoint> bootstrap(endpoint);
    const char *bsargv[] = {reinterpret_cast<const char *>(&bootstrap),
                            nullptr};
    int fd                = kmod_fopen("/initrd/test_rpc_client.mod", "x");
    CapIdx client_pcb = cap::error;
    if (fd >= 0) {
        ExecveRequest request{
            .image_cap = kmod_getcap(fd),
            .execfn    = nullptr,
            .caps      = initial_caps,
            .argv      = nullptr,
            .envp      = nullptr,
            .bsargv    = bsargv,
        };
        auto client_pcb_res =
            sys_create_process(SCHED_CLASS_RR, &request)
                .to_result();
        if (client_pcb_res.has_value()) {
            client_pcb = client_pcb_res.value();
        }
    }
    if (fd >= 0) {
        kmod_fclose(fd);
    }
    if (client_pcb == cap::error) {
        printf("Failed to create test_rpc_client\n");
        exit(0);
    }
    (void)sys_cap_remove(client_pcb).to_result();
    printf("test_rpc_server: client started\n");

    while (true) {
        MsgPacket recv_msg{
            .msgsz = MAX_MSG_SIZE,
            .capsz = MAX_MSG_CAPS,
        };
        (void)sys_endpoint_recv(endpoint, &recv_msg).to_result();
        if (rpc::is_rpc_message(recv_msg)) {
            server.handle_message(recv_msg);
        } else {
            printf("Received non-RPC message, ignoring...\n");
        }
    }

    return 0;
}
