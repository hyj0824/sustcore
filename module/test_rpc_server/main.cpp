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

int kmod_main() {
    CapIdx endpoint = sys_endpoint_create();
    if (endpoint == cap::error) {
        printf("Failed to create endpoint\n");
        exit(0);
    }

    SampleServer server(endpoint);
    printf("Server is running endpoint=%p\n", (void *)endpoint);

    CapIdx initial_caps[] = {endpoint};
    CapIdx client_pcb     = sys_create_process("/initrd/test_rpc_client.mod",
                                               initial_caps, 1,
                                               SCHED_CLASS_FCFS);
    if (client_pcb == cap::error) {
        printf("Failed to create test_rpc_client\n");
        exit(0);
    }
    sys_cap_remove(client_pcb);
    printf("test_rpc_server: client started\n");

    while (true) {
        MsgPacket recv_msg{
            .msgsz = MAX_MSG_SIZE,
            .capsz = MAX_MSG_CAPS,
        };
        sys_endpoint_recv(endpoint, &recv_msg);
        if (rpc::is_rpc_message(recv_msg)) {
            server.handle_message(recv_msg);
        } else {
            printf("Received non-RPC message, ignoring...\n");
        }
    }

    return 0;
}
