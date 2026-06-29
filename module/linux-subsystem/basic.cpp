/**
 * @file basic.cpp
 * @author theflysong
 * @brief linux subsystem 中用户程序基础运行时逻辑
 * @version alpha-1.0.0
 * @date 2026-06-22
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <errno.h>
#include <fdtable.h>
#include <file.h>
#include <logger.h>
#include <prm.h>
#include <prog.h>
#include <syscall.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
    constexpr size_t INVALID_VALUE      = 0xFFFF'FFFF'FFFF'FFFF;
    constexpr size_t UTSNAME_FIELD_SIZE = 65;
    constexpr unsigned long LINUX_PERSONALITY_QUERY = 0xFFFFFFFFUL;
    constexpr unsigned long PER_LINUX               = 0x0000UL;
    constexpr int LINUX_O_WRONLY        = 1;
    constexpr int LINUX_SIGABRT         = 6;
    constexpr int LINUX_SIGKILL         = 9;
    constexpr int LINUX_SIGSEGV         = 11;
    constexpr int LINUX_SIGTERM         = 15;
    constexpr long LINUX_TIMES_STAMP    = 114514;
    constexpr byte GETRANDOM_PATTERN[]  = {0x11, 0x41, 0x51, 0x41,
                                           0x19, 0x19, 0x81, 0x00};
    constexpr int SYSLOG_ACTION_CLOSE         = 0;
    constexpr int SYSLOG_ACTION_OPEN          = 1;
    constexpr int SYSLOG_ACTION_READ          = 2;
    constexpr int SYSLOG_ACTION_READ_ALL      = 3;
    constexpr int SYSLOG_ACTION_READ_CLEAR    = 4;
    constexpr int SYSLOG_ACTION_CLEAR         = 5;
    constexpr int SYSLOG_ACTION_CONSOLE_OFF   = 6;
    constexpr int SYSLOG_ACTION_CONSOLE_ON    = 7;
    constexpr int SYSLOG_ACTION_CONSOLE_LEVEL = 8;
    constexpr int SYSLOG_ACTION_SIZE_UNREAD   = 9;
    constexpr int SYSLOG_ACTION_SIZE_BUFFER   = 10;

    struct linux_utsname {
        char sysname[UTSNAME_FIELD_SIZE];
        char nodename[UTSNAME_FIELD_SIZE];
        char release[UTSNAME_FIELD_SIZE];
        char version[UTSNAME_FIELD_SIZE];
        char machine[UTSNAME_FIELD_SIZE];
        char domainname[UTSNAME_FIELD_SIZE];
    };

    struct linux_timeval {
        uint64_t sec;
        uint64_t usec;
    };

    struct linux_tms {
        long tms_utime;
        long tms_stime;
        long tms_cutime;
        long tms_cstime;
    };

    struct ShellIoConfig {
        bool present = false;
        bool append  = false;
        CapIdx cap   = cap::null;
        std::string path{};
    };

    bool parse_tagged_index(const char *text, const char *prefix,
                            size_t &value) {
        if (text == nullptr || prefix == nullptr) {
            return false;
        }
        size_t prefix_len = strlen(prefix);
        if (strncmp(text, prefix, prefix_len) != 0) {
            return false;
        }
        value = 0;
        for (const char *p = text + prefix_len; *p != '\0'; ++p) {
            if (*p < '0' || *p > '9') {
                return false;
            }
            value = value * 10 + static_cast<size_t>(*p - '0');
        }
        return true;
    }

    bool has_memory_kind(const char *desc, const char *kind) {
        if (desc == nullptr || kind == nullptr || desc[0] != '#') {
            return false;
        }
        ++desc;
        size_t kind_len = strlen(kind);
        return strncmp(desc, kind, kind_len) == 0 && desc[kind_len] == ':';
    }

    void copy_uts_field(char (&dst)[UTSNAME_FIELD_SIZE],
                        const char *src) noexcept {
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, UTSNAME_FIELD_SIZE - 1);
        dst[UTSNAME_FIELD_SIZE - 1] = '\0';
    }

    const char *linux_machine_name() noexcept {
#if defined(__ARCH_riscv64__)
        return "riscv64";
#elif defined(__ARCH_loongarch64__)
        return "loongarch64";
#else
        return "unknown";
#endif
    }

    [[nodiscard]]
    bool find_direct_child_cap_by_pid(int pid, CapIdx &pcb_cap) noexcept {
        for (auto child_cap : __prog_children) {
            auto pid_res = sys_getpid(child_cap).to_result();
            if (!pid_res.has_value()) {
                continue;
            }
            if (static_cast<int>(pid_res.value()) == pid) {
                pcb_cap = child_cap;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]]
    int signal_exit_code(int sig) noexcept {
        switch (sig) {
            case LINUX_SIGABRT:
            case LINUX_SIGKILL:
            case LINUX_SIGSEGV:
            case LINUX_SIGTERM: return 128 + sig;
            default:            return -1;
        }
    }
}  // namespace

void init_procfs() {
    if (__prog_pcb_cap == cap::null || __prog_pcb_cap == cap::error) {
        return;
    }

    auto comm_cap_res = sys_pcb_procfs_get(__prog_pcb_cap, "comm").to_result();
    if (comm_cap_res.has_value()) {
        const char *comm =
            __prog_image_path.empty() ? "<linuxss>" : __prog_image_path.c_str();
        size_t comm_len = strlen(comm);
        if (comm_len != 0) {
            (void)sys_vfs_write(comm_cap_res.value(), 0, comm, comm_len)
                .to_result();
        }
        (void)sys_cap_remove(comm_cap_res.value()).to_result();
    }

    if (!__prog_image_path.empty()) {
        if (!sys_pcb_procfs_redirect(__prog_pcb_cap, "exe",
                                     __prog_image_path.c_str())
                 .to_result())
        {
            loggers::LXSC::ERROR("Failed to redirect /proc/<pid>/exe");
        }
    }
    if (!__prog_cwd.empty()) {
        if (!sys_pcb_procfs_redirect(__prog_pcb_cap, "cwd", __prog_cwd.c_str())
                 .to_result())
        {
            loggers::LXSC::ERROR("Failed to redirect /proc/<pid>/cwd");
        }
    }
    if (!sys_pcb_procfs_redirect(__prog_pcb_cap, "root", "/").to_result()) {
        loggers::LXSC::ERROR("Failed to redirect /proc/<pid>/root");
    }
}

size_t __prog_heap_base    = 0;
size_t __prog_brk          = 0;
CapIdx __prog_pcb_cap      = cap::null;
CapIdx __prog_parent_cap   = cap::null;
CapIdx __prog_main_tcb_cap = cap::null;
CapIdx __prog_heap_mem_cap = cap::null;
CapIdx __prog_root_dir_cap = cap::null;
CapIdx __prog_cwd_dir_cap  = cap::null;
std::vector<CapIdx> __prog_children{};
std::string __prog_cwd = "/";
std::string __prog_image_path{};
unsigned long __prog_personality = PER_LINUX;
ShellIoConfig __prog_stdout{};
ShellIoConfig __prog_stderr{};

namespace {
    void reset_shellio_config(ShellIoConfig &config) {
        config.present = false;
        config.append  = false;
        config.cap     = cap::null;
        config.path.clear();
    }

    void init_shell_stdio() {
        const char *default_stdout = "/dev/stdout";
        bool stdout_append =
            __prog_stdout.present ? __prog_stdout.append : false;

        if (cap::valid(__prog_stdout.cap)) {
            loggers::LXSC::INFO(
                "linux-subsystem: binding STDOUT fd to cap 0x%016X",
                __prog_stdout.cap);
            (void)linux_bind_cap_fd(__prog_stdout.cap, 1, stdout_append);
        } else {
            const char *stdout_path = __prog_stdout.path.empty()
                                          ? default_stdout
                                          : __prog_stdout.path.c_str();
            loggers::LXSC::INFO("linux-subsystem: binding STDOUT fd to path %s",
                                stdout_path);
            (void)linux_open_fd(stdout_path, 1, LINUX_O_WRONLY);
        }

        bool stderr_append =
            __prog_stderr.present ? __prog_stderr.append : stdout_append;
        if (__prog_stderr.present && cap::valid(__prog_stderr.cap)) {
            loggers::LXSC::INFO(
                "linux-subsystem: binding STDERR fd to cap 0x%016X",
                __prog_stderr.cap);
            (void)linux_bind_cap_fd(__prog_stderr.cap, 2, stderr_append);
            return;
        }
        if (__prog_stderr.present && !__prog_stderr.path.empty()) {
            loggers::LXSC::INFO("linux-subsystem: binding STDERR fd to path %s",
                                __prog_stderr.path.c_str());
            (void)linux_open_fd(__prog_stderr.path.c_str(), 2, LINUX_O_WRONLY);
            return;
        }

        CapIdx stdout_cap = fd_to_cap(1);
        if (cap::valid(stdout_cap)) {
            loggers::LXSC::INFO(
                "linux-subsystem: binding STDERR fd to STDOUT cap 0x%016X",
                stdout_cap);
            (void)linux_bind_cap_fd(stdout_cap, 2, stdout_append);
        } else {
            const char *stdout_path = __prog_stdout.path.empty()
                                          ? default_stdout
                                          : __prog_stdout.path.c_str();
            loggers::LXSC::INFO(
                "linux-subsystem: binding STDERR fd to stdout path %s",
                stdout_path);
            (void)linux_open_fd(stdout_path, 2, LINUX_O_WRONLY);
        }
    }
}  // namespace

void init_prog_data(size_t argc, const char *argv[], size_t bsargc,
                    const bsheader *bsargv[]) {
    __prog_heap_base    = 0;
    __prog_brk          = 0;
    __prog_pcb_cap      = cap::null;
    __prog_parent_cap   = cap::null;
    __prog_main_tcb_cap = cap::null;
    __prog_heap_mem_cap = cap::null;
    __prog_root_dir_cap = cap::null;
    __prog_cwd_dir_cap  = cap::null;
    __prog_children.clear();
    __prog_cwd = "/";
    __prog_image_path.clear();
    __prog_personality = PER_LINUX;
    reset_shellio_config(__prog_stdout);
    reset_shellio_config(__prog_stderr);

    for (size_t i = 0; i < bsargc; ++i) {
        BootstrapRecordView view{};
        if (!bootstrap_make_view(bsargv[i], view)) {
            continue;
        }

        if (view.header->type == boot::TYPE_CAPEXP) {
            BootstrapCapExplainView cap_view{};
            if (!bootstrap_parse_cap_explain(view, cap_view)) {
                continue;
            }

            size_t tagged_idx = 0;
            if (cap_view.cap_type == PayloadType::PCB &&
                parse_tagged_index(cap_view.cap_desc, "#self:", tagged_idx))
            {
                __prog_pcb_cap = cap_view.cap_idx;
                continue;
            }
            if (cap_view.cap_type == PayloadType::PCB &&
                strcmp(cap_view.cap_desc, "#parent") == 0)
            {
                __prog_parent_cap = cap_view.cap_idx;
                continue;
            }
            if (cap_view.cap_type == PayloadType::TCB &&
                parse_tagged_index(cap_view.cap_desc, "#main:", tagged_idx))
            {
                __prog_main_tcb_cap = cap_view.cap_idx;
                continue;
            }
            if (cap_view.cap_type == PayloadType::MEMORY &&
                has_memory_kind(cap_view.cap_desc, "heap"))
            {
                __prog_heap_mem_cap = cap_view.cap_idx;
                continue;
            }
            if (cap_view.cap_type == PayloadType::VDIR &&
                cap_view.cap_desc != nullptr && cap_view.cap_desc[0] == '#')
            {
                if (strcmp(cap_view.cap_desc + 1, "/") == 0) {
                    __prog_root_dir_cap = cap_view.cap_idx;
                } else if (strcmp(cap_view.cap_desc, "#cwd") == 0) {
                    __prog_cwd_dir_cap = cap_view.cap_idx;
                }
                continue;
            }
            if (cap_view.cap_type == PayloadType::VFILE &&
                cap_view.cap_desc != nullptr && cap_view.cap_desc[0] == '#')
            {
                if (strcmp(cap_view.cap_desc, "#stdout-cap") == 0) {
                    __prog_stdout.cap = cap_view.cap_idx;
                } else if (strcmp(cap_view.cap_desc, "#stderr-cap") == 0) {
                    __prog_stderr.cap = cap_view.cap_idx;
                }
                continue;
            }
            continue;
        }

        if (view.header->type == boot::TYPE_VADDREXP) {
            BootstrapVaddrExplainView vaddr_view{};
            if (!bootstrap_parse_vaddr_explain(view, vaddr_view)) {
                continue;
            }
            if (strcmp(vaddr_view.vaddr_desc, "#heap") == 0) {
                __prog_heap_base = vaddr_view.vaddr.arith();
                __prog_brk       = vaddr_view.vaddr.arith();
            }
            continue;
        }

        if (view.header->type == boot::TYPE_PATHEXP) {
            BootstrapPathExplainView path_view{};
            if (!bootstrap_parse_path_explain(view, path_view)) {
                continue;
            }
            if (strncmp(path_view.path_desc, "#cwd:", 5) == 0) {
                __prog_cwd = path_view.path_desc + 5;
            } else if (strncmp(path_view.path_desc, "#exe:", 5) == 0) {
                __prog_image_path = path_view.path_desc + 5;
            } else if (strncmp(path_view.path_desc, "#stdout:", 8) == 0) {
                __prog_stdout.path = path_view.path_desc + 8;
            } else if (strncmp(path_view.path_desc, "#stderr:", 8) == 0) {
                __prog_stderr.path = path_view.path_desc + 8;
            }
            continue;
        }

        if (view.header->type == boot::TYPE_SHELLIO) {
            BootstrapShellIoView shellio_view{};
            if (!bootstrap_parse_shell_io(view, shellio_view)) {
                continue;
            }
            ShellIoConfig *target = nullptr;
            if (shellio_view.target == boot::SHELLIO_TARGET_STDOUT) {
                target = &__prog_stdout;
            } else if (shellio_view.target == boot::SHELLIO_TARGET_STDERR) {
                target = &__prog_stderr;
            }
            if (target != nullptr) {
                target->present = true;
                target->append =
                    shellio_view.flags == boot::SHELLIO_FLAG_APPEND;
            }
        }
    }

    if (__prog_image_path.empty() && argc != 0 && argv != nullptr &&
        argv[0] != nullptr)
    {
        __prog_image_path = argv[0];
    }

    if (__prog_cwd_dir_cap == cap::null || __prog_cwd_dir_cap == cap::error) {
        (void)linux_opendir_fd(__prog_cwd.c_str(), CWD_FD);
        __prog_cwd_dir_cap = fd_to_cap(CWD_FD);
    }

    init_procfs();
    init_shell_stdio();
}

size_t linux_sys_brk(size_t newbrk) {
    if (newbrk == 0) {
        return __prog_brk;
    }

    if (newbrk < __prog_heap_base) {
        return __prog_brk;
    }
    if (__prog_heap_mem_cap == cap::null) {
        return __prog_brk;
    }
    if (!sys_mem_resize(__prog_heap_mem_cap, newbrk - __prog_heap_base)) {
        return __prog_brk;
    }

    __prog_brk = newbrk;
    return __prog_brk;
}

size_t linux_sys_uname(void *buf) {
    if (buf == nullptr) {
        return INVALID_VALUE;
    }

    linux_utsname uts{};
    copy_uts_field(uts.sysname, "Linux");
    copy_uts_field(uts.nodename, "qemu");
    copy_uts_field(uts.release, "4.15.0");
    copy_uts_field(uts.version, "build 0");
    copy_uts_field(uts.machine, linux_machine_name());
    copy_uts_field(uts.domainname, "(none)");
    memcpy(buf, &uts, sizeof(uts));
    return 0;
}

size_t linux_sys_gettimeofday(void *tv, void *) {
    if (tv == nullptr) {
        return 0;
    }

    auto now_res = sys_getrtctime().to_result();
    if (!now_res.has_value()) {
        return INVALID_VALUE;
    }

    auto now_ns = now_res.value();
    linux_timeval value{
        .sec  = static_cast<uint64_t>(now_ns / 1000000000ULL),
        .usec = static_cast<uint64_t>((now_ns % 1000000000ULL) / 1000ULL),
    };
    memcpy(tv, &value, sizeof(value));
    return 0;
}

size_t linux_sys_clock_gettime(int clk_id, void *tp) {
    // 目前 CLOCK_REALTIME 和 CLOCK_MONOTONIC 都以 rtc 作为时间源，返回的时间值相同
    if (clk_id != 0 && clk_id != 1) {
        loggers::LXSC::ERROR("only supported clock ids are CLOCK_REALTIME (0) and CLOCK_MONOTONIC (1), but get %d", clk_id);
        return static_cast<size_t>(-ENOSYS);
    }
    if (tp == nullptr) {
        loggers::LXSC::ERROR("clock_gettime tp is nullptr");
        return -EINVAL;
    }

    auto now_res = sys_getrtctime().to_result();
    if (!now_res.has_value()) {
        loggers::LXSC::ERROR("clock_gettime failed to get current time");
        return -EINVAL;
    }

    struct linux_timespec {
        uint64_t sec;
        uint64_t nsec;
    } value{
        .sec  = static_cast<uint64_t>(now_res.value() / 1000000000ULL),
        .nsec = static_cast<uint64_t>(now_res.value() % 1000000000ULL),
    };
    memcpy(tp, &value, sizeof(value));
    return 0;
}

size_t linux_sys_personality(unsigned long persona) {
    unsigned long previous = __prog_personality;
    if (persona == LINUX_PERSONALITY_QUERY) {
        return previous;
    }
    __prog_personality = static_cast<uint32_t>(persona);
    return previous;
}

size_t linux_sys_syslog(int type, void *bufp, int len) {
    switch (type) {
        case SYSLOG_ACTION_CLOSE:
        case SYSLOG_ACTION_OPEN:
        case SYSLOG_ACTION_CLEAR:
        case SYSLOG_ACTION_CONSOLE_OFF:
        case SYSLOG_ACTION_CONSOLE_ON:
        case SYSLOG_ACTION_CONSOLE_LEVEL:
            return 0;
        case SYSLOG_ACTION_SIZE_UNREAD:
        case SYSLOG_ACTION_SIZE_BUFFER:
            return 0;
        case SYSLOG_ACTION_READ:
        case SYSLOG_ACTION_READ_ALL:
        case SYSLOG_ACTION_READ_CLEAR:
            if (len < 0) {
                return -EINVAL;
            }
            if (bufp == nullptr && len != 0) {
                return -EFAULT;
            }
            return 0;
        default:
            return -EINVAL;
    }
}

size_t linux_sys_times(void *buf) {
    auto now_res = sys_time_now_ns().to_result();
    if (!now_res.has_value()) {
        return INVALID_VALUE;
    }

    if (buf != nullptr) {
        linux_tms tms{
            .tms_utime  = LINUX_TIMES_STAMP,
            .tms_stime  = LINUX_TIMES_STAMP,
            .tms_cutime = LINUX_TIMES_STAMP,
            .tms_cstime = LINUX_TIMES_STAMP,
        };
        memcpy(buf, &tms, sizeof(tms));
    }
    return now_res.value() / 1000000ULL;
}

size_t linux_sys_getrandom(void *buf, size_t buflen, unsigned flags) {
    (void)flags;

    if (buf == nullptr && buflen != 0) {
        return INVALID_VALUE;
    }
    if (buflen == 0) {
        return 0;
    }

    auto *bytes = reinterpret_cast<byte *>(buf);
    for (size_t i = 0; i < buflen; ++i) {
        bytes[i] = GETRANDOM_PATTERN[i % sizeof(GETRANDOM_PATTERN)];
    }
    return buflen;
}

size_t linux_sys_nanosleep(const void *req, void *rem) {
    if (req == nullptr) {
        return INVALID_VALUE;
    }

    auto *tv       = reinterpret_cast<const linux_timeval *>(req);
    uint64_t ns    = tv->sec * 1000000000ULL + tv->usec * 1000ULL;
    auto sleep_res = sys_tcb_nanosleep(static_cast<size_t>(ns)).to_result();
    if (!sleep_res.has_value()) {
        return INVALID_VALUE;
    }

    if (rem != nullptr) {
        linux_timeval zero{};
        memcpy(rem, &zero, sizeof(zero));
    }
    return 0;
}

[[noreturn]]
void linux_sys_exit(int exitcode) {
    (void)sys_tcb_kill(__prog_main_tcb_cap, exitcode).to_result();
    loggers::LXRT::ERROR("sys_exit 返回到不应执行的位置: exitcode=%d",
                         exitcode);
    while (true);
}

[[noreturn]]
void linux_sys_exit_group(int exitcode) {
    (void)sys_pcb_kill(__prog_pcb_cap, exitcode).to_result();
    loggers::LXRT::ERROR("sys_exit_group 返回到不应执行的位置: exitcode=%d",
                         exitcode);
    while (true);
}

size_t linux_sys_getpid() {
    if (!cap::valid(__prog_pcb_cap)) {
        return INVALID_VALUE;
    }
    return sys_getpid(__prog_pcb_cap).value();
}

size_t linux_sys_getppid() {
    if (!cap::valid(__prog_parent_cap)) {
        return 0;
    }
    return sys_getpid(__prog_parent_cap).value();
}

size_t linux_sys_gettid() {
    if (!cap::valid(__prog_main_tcb_cap)) {
        return static_cast<size_t>(-EINVAL);
    }

    auto tid_res = sys_tcb_get_tid(__prog_main_tcb_cap).to_result();
    if (!tid_res.has_value()) {
        loggers::LXSC::ERROR("gettid failed: err=%s",
                             to_cstring(tid_res.error()));
        return static_cast<size_t>(-EINVAL);
    }
    return tid_res.value();
}

size_t linux_sys_kill(int pid, int sig) {
    loggers::LXSC::INFO("kill pid=%d sig=%d", pid, sig);
    int exit_code = signal_exit_code(sig);
    if (exit_code < 0) {
        loggers::LXSC::ERROR(
            "kill 仅支持 SIGABRT/SIGKILL/SIGSEGV/SIGTERM, 目前得到 sig=%d",
            sig);
        return static_cast<size_t>(-ENOSYS);
    }
    if (pid <= 0) {
        loggers::LXSC::ERROR("kill 不支持 pid=%d", pid);
        return static_cast<size_t>(-ENOSYS);
    }
    if (!cap::valid(__prog_pcb_cap)) {
        loggers::LXSC::ERROR("当前 pcb cap 无效, 无法执行 kill");
        return static_cast<size_t>(-EINVAL);
    }

    auto self_pid_res = sys_getpid(__prog_pcb_cap).to_result();
    if (!self_pid_res.has_value()) {
        loggers::LXSC::ERROR("kill failed to resolve self pid: err=%s",
                             to_cstring(self_pid_res.error()));
        return static_cast<size_t>(-EINVAL);
    }

    CapIdx target_pcb = cap::null;
    if (static_cast<int>(self_pid_res.value()) == pid) {
        target_pcb = __prog_pcb_cap;
    } else if (!find_direct_child_cap_by_pid(pid, target_pcb)) {
        loggers::LXSC::ERROR("kill target pid=%d not found", pid);
        return static_cast<size_t>(-ESRCH);
    }

    auto kill_res = sys_pcb_kill(target_pcb, exit_code).to_result();
    if (!kill_res.has_value()) {
        loggers::LXSC::ERROR("kill failed pid=%d err=%s", pid,
                             to_cstring(kill_res.error()));
        return static_cast<size_t>(-EINVAL);
    }
    return 0;
}

size_t linux_sys_tgkill(int tgid, int tid, int sig) {
    loggers::LXSC::INFO("tgkill tgid=%d tid=%d sig=%d", tgid, tid, sig);
    int exit_code = signal_exit_code(sig);
    if (exit_code < 0) {
        loggers::LXSC::ERROR(
            "tgkill 仅支持 SIGABRT/SIGKILL/SIGSEGV/SIGTERM, 目前得到 sig=%d",
            sig);
        return static_cast<size_t>(-ENOSYS);
    }
    if (!cap::valid(__prog_pcb_cap) || !cap::valid(__prog_main_tcb_cap)) {
        loggers::LXSC::ERROR("tgkill PCB 与 TCB 能力无效");
        return static_cast<size_t>(-EINVAL);
    }

    auto self_pid_res = sys_getpid(__prog_pcb_cap).to_result();
    auto self_tid_res = sys_tcb_get_tid(__prog_main_tcb_cap).to_result();
    if (!self_pid_res.has_value() || !self_tid_res.has_value()) {
        loggers::LXSC::ERROR(
            "tgkill 无法获取当前 PCB 或 TCB 的 pid/tid: err_pid=%s err_tid=%s",
            to_cstring(self_pid_res.error()), to_cstring(self_tid_res.error()));
        return static_cast<size_t>(-EINVAL);
    }

    if (static_cast<int>(self_pid_res.value()) != tgid ||
        static_cast<int>(self_tid_res.value()) != tid)
    {
        loggers::LXSC::ERROR(
            "tgkill 目标不匹配: want tgid=%d tid=%d current_tgid=%lu "
            "current_tid=%lu",
            tgid, tid, static_cast<unsigned long>(self_pid_res.value()),
            static_cast<unsigned long>(self_tid_res.value()));
        return static_cast<size_t>(-ESRCH);
    }

    auto kill_res = sys_tcb_kill(__prog_main_tcb_cap, exit_code).to_result();
    if (!kill_res.has_value()) {
        loggers::LXSC::ERROR("tgkill failed tgid=%d tid=%d err=%s", tgid, tid,
                             to_cstring(kill_res.error()));
        return static_cast<size_t>(-EINVAL);
    }
    return 0;
}

size_t linux_sys_sched_yield() {
    auto yield_res = sys_yield().to_result();
    if (!yield_res.has_value()) {
        return INVALID_VALUE;
    }
    return yield_res.value() != 0 ? INVALID_VALUE : 0;
}
