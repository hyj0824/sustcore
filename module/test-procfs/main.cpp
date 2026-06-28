/**
 * @file main.cpp
 * @author theflysong
 * @brief procfs 基础测试
 * @version alpha-1.0.0
 * @date 2026-06-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <kmod/syscall.h>

#include <cstdio>
#include <cstring>

namespace {
    void fail(const char *msg) {
        printf("test_procfs: FAIL %s\n", msg);
        exit(-1);
    }

    void check(bool cond, const char *msg) {
        if (!cond) {
            fail(msg);
        }
    }

    [[nodiscard]]
    size_t read_all(CapIdx cap, char *buf, size_t bufsz) {
        size_t total = 0;
        while (total < bufsz) {
            auto read_res =
                sys_vfs_read(cap, total, buf + total, bufsz - total).to_result();
            check(read_res.has_value(), "sys_vfs_read failed");
            if (read_res.value() == 0) {
                break;
            }
            total += read_res.value();
        }
        return total;
    }
}  // namespace

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
                         const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;

    printf("test_procfs: try accessing procfs_get(\"/\")\n");
    auto self_cap_res = sys_pcb_procfs_get(__pcb_cap, "/").to_result();
    check(self_cap_res.has_value(), "sys_pcb_procfs_get(/) failed");

    printf("test_procfs: try accessing /proc/self/\n");
    char pathbuf[64]{};
    auto readlink_res =
        sys_vfs_readlink(self_cap_res.value(), "../self", pathbuf, sizeof(pathbuf))
            .to_result();
    check(readlink_res.has_value(), "readlink ../self failed");
    check(readlink_res.value() > 0, "readlink ../self empty");

    printf("test_procfs: try accessing /proc/self/comm\n");
    auto comm_cap_res = sys_pcb_procfs_get(__pcb_cap, "comm").to_result();
    check(comm_cap_res.has_value(), "sys_pcb_procfs_get(comm) failed");
    const char *comm = "test-procfs";
    auto write_res =
        sys_vfs_write(comm_cap_res.value(), 0, comm, strlen(comm)).to_result();
    check(write_res.has_value(), "write comm failed");

    char comm_buf[64]{};
    size_t comm_size = read_all(comm_cap_res.value(), comm_buf, sizeof(comm_buf));
    check(comm_size == strlen(comm), "comm size mismatch");
    check(memcmp(comm_buf, comm, strlen(comm)) == 0, "comm content mismatch");

    printf("test_procfs: try accessing /proc/self/cmdline\n");
    auto cmdline_cap_res = sys_pcb_procfs_get(__pcb_cap, "cmdline").to_result();
    check(cmdline_cap_res.has_value(), "sys_pcb_procfs_get(cmdline) failed");
    auto cmdline_write_res =
        sys_vfs_write(cmdline_cap_res.value(), 0, comm, strlen(comm)).to_result();
    check(!cmdline_write_res.has_value() &&
              cmdline_write_res.error() == ErrCode::INSUFFICIENT_PERMISSIONS,
          "cmdline unexpectedly writable");

    printf("test_procfs: try redirecting /proc/self/exe -> /initrd/test-procfs.mod\n");
    auto redirect_res =
        sys_pcb_procfs_redirect(__pcb_cap, "exe", "/initrd/test-procfs.mod")
            .to_result();
    check(redirect_res.has_value(), "redirect exe failed");

    char exe_buf[128]{};
    auto exe_readlink_res =
        sys_vfs_readlink(self_cap_res.value(), "exe", exe_buf, sizeof(exe_buf))
            .to_result();
    check(exe_readlink_res.has_value(), "readlink exe failed");
    check(strcmp(exe_buf, "/initrd/test-procfs.mod") == 0,
          "exe redirect mismatch");

    printf("test_procfs: PASS\n");
    exit(0);
    return 0;
}
