#include <kmod/bootstrap.h>
#include <kmod/syscall.h>

#include <cstddef>
#include <cstdio>

constexpr size_t kSignalSyn    = 0;
constexpr size_t kSignalSynAck = 1;
constexpr size_t kSignalAck    = 2;
constexpr uint32_t kBootstrapTypeNotif = 0xFFFF0002U;

static CapIdx bootstrap_notif() {
    CapIdx notif = cap::null;
    if (!bootstrap_find_single_cap(__bsargv, __bsargc, kBootstrapTypeNotif,
                                   notif))
    {
        printf("test_execve: 启动参数无效 bsargc=%u\n", __bsargc);
        exit(-1);
    }
    return notif;
}

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;
    printf("test_execve: pid=%u pcb_cap=%p\n", sys_getpid(__pcb_cap),
           (void *)__pcb_cap);

    CapIdx notif_cap = bootstrap_notif();
    printf("test_execve: notification cap=%p\n", (void *)notif_cap);

    printf("test_execve: 等待 SYN\n");
    sys_notif_wait(notif_cap, kSignalSyn);

    printf("test_execve: 接收 SYN\n");
    sys_notif_unsignal(notif_cap, kSignalSyn);

    printf("test_execve: 发送 SYN-ACK\n");
    sys_notif_signal(notif_cap, kSignalSynAck);

    printf("test_execve: 等待 ACK\n");
    sys_notif_wait(notif_cap, kSignalAck);

    printf("test_execve: 接收并取消 ACK\n");
    sys_notif_unsignal(notif_cap, kSignalAck);

    printf("test_execve: 握手完成!\n");
    exit(0);
    return 0;
}
