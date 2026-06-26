#include <sustcore/bootstrap.h>
#include <kmod/syscall.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

static volatile size_t global_value = 0;

constexpr size_t kScanGroups         = 1;
constexpr size_t kScanSlots          = 32;
constexpr size_t kSignalSyn          = 0;
constexpr size_t kSignalSynAck       = 1;
constexpr size_t kSignalAck          = 2;
constexpr size_t kCompletionSignal   = 0;
static CapIdx exec_notif_cap         = cap::null;
constexpr uint32_t kBootstrapTypeNotif = 0xFFFF0002U;

static const char *cap_type_name(PayloadType type) {
    return to_string(type);
}

static void dump_caps(const char *tag) {
    printf("%s: capability dump begin\n", tag);
    for (size_t group = 0; group < kScanGroups; ++group) {
        for (size_t slot = 0; slot < kScanSlots; ++slot) {
            CapIdx idx = cap::make(group, slot);
            CapInfo info{};
            if (!sys_cap_lookup(idx, &info)) {
                continue;
            }
            printf("%s: 编号=%p 类型=%s 权限=%p\n", tag, (void *)idx,
                   cap_type_name(info.type), (void *)info.permissions);
        }
    }
    printf("%s: capability dump end\n", tag);
}

static char *alloc_page_string(const char *str) {
    char *buf = static_cast<char *>(sbrk(4096));
    if (buf == reinterpret_cast<char *>(-1)) {
        printf("test_fork: sbrk failed\n");
        exit(-1);
    }
    strcpy(buf, str);
    return buf;
}

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
                         const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;
    printf("test_fork: 启动时PID=%u pcb_cap=%p\n", sys_getpid(__pcb_cap).value(),
           (void *)__pcb_cap);

    auto notif_res = sys_notif_create().to_result();
    exec_notif_cap = notif_res.has_value() ? notif_res.value() : cap::error;
    if (exec_notif_cap == cap::error) {
        printf("test_fork: create exec notification failed\n");
        exit(-1);
    }

    global_value     = 114514;
    char *shared_buf = alloc_page_string("全体目光向我看齐");

    CapIdx child_pcb_cap = cap::null;
    auto fork_res        = fork(&child_pcb_cap).to_result();
    if (!fork_res.has_value() || child_pcb_cap == cap::error) {
        printf("test_fork: fork failed\n");
        exit(-1);
    }
    size_t child_pid = fork_res.value();

    bool is_child        = child_pid == 0;
    const char *tag      = is_child ? "child" : "parent";
    size_t abi_pcb_pid   = sys_getpid(__pcb_cap).value();
    size_t child_cap_pid = sys_getpid(child_pcb_cap).value();
    printf(
        "test_fork(%s): fork后 子进程capidx=%p 子进程pid=%u ABI获得的PCB PID=%u "
        "子进程PID=%u global=%u shared=%s\n",
        tag, (void *)child_pcb_cap, child_pid, abi_pcb_pid, child_cap_pid,
        global_value, shared_buf);

    if (is_child) {
        global_value = 1919;
        strcpy(shared_buf, "看我看我");
    } else {
        global_value = 810;
        strcpy(shared_buf, "我宣布个事");
    }

    printf("test_fork(%s): COW写后 global=%u shared=%s\n", tag, global_value,
           shared_buf);

    char *private_buf = alloc_page_string("ABC");
    if (is_child) {
        strcpy(private_buf, "XYZ");
    } else {
        strcpy(private_buf, "UVW");
    }
    printf("test_fork(%s): private buf=%s\n", tag, private_buf);

    dump_caps(tag);

    if (is_child) {
        CapIdx reserved_caps[] = {exec_notif_cap, cap::null};
        BootstrapSingleCapRecord<kBootstrapTypeNotif> bootstrap(exec_notif_cap);
        const char *bsargv[] = {reinterpret_cast<const char *>(&bootstrap),
                                nullptr};
        printf("test_fork(%s): child exec test_execve\n", tag);
        int fd = kmod_fopen("/initrd/test_execve.mod", "x");
        if (fd < 0 ||
            !execve(kmod_getcap(fd), reserved_caps, nullptr, nullptr, bsargv)
                 .to_result()
                 .has_value())
        {
            printf("test_fork(%s): child exec failed\n", tag);
        }
        if (fd >= 0) {
            kmod_fclose(fd);
        }
        exit(-1);
    }

    printf("test_fork(%s): 发送 SYN\n", tag);
    (void)sys_notif_signal(exec_notif_cap, kSignalSyn).to_result();

    printf("test_fork(%s): 等待 SYN-ACK\n", tag);
    (void)sys_notif_wait(exec_notif_cap, kSignalSynAck).to_result();

    printf("test_fork(%s): 接收 SYN-ACK\n", tag);
    (void)sys_notif_unsignal(exec_notif_cap, kSignalSynAck).to_result();

    printf("test_fork(%s): 发送 ACK\n", tag);
    (void)sys_notif_signal(exec_notif_cap, kSignalAck).to_result();

    printf("test_fork(%s): exit\n", tag);
    exit(0);

    return 0;
}
