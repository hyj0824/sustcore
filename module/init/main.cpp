/**
 * @file main.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 主文件
 * @version alpha-1.0.0
 * @date 2026-04-28
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <kmod/bootstrap.h>
#include <kmod/syscall.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
    // 在 bootstrap 信息中寻找根目录能力
    [[nodiscard]]
    CapIdx bootstrap_root_dir() {
        CapIdx cap = cap::null;
        bool found = false;
        bool ok    = bootstrap_foreach_record(
            __startup_data, __startup_size, [&](const BootstrapRecordView &view) {
                if (found || view.header->type != BOOTSTRAP_TYPE_DIRCAPEXPLAIN)
                {
                    return;
                }
                BootstrapCapPathView cap_path{};
                if (!bootstrap_parse_cap_path(view, cap_path)) {
                    return;
                }
                if (strcmp(cap_path.path, "/") != 0) {
                    return;
                }
                cap   = cap_path.cap;
                found = true;
            });
        return ok && found ? cap : cap::null;
    }

    // 创建新进程并传递根目录能力
    [[nodiscard]]
    CapIdx spawn_with_root_dir(int fd, size_t sched_class, CapIdx root_dir_cap) {
        if (fd < 0 || root_dir_cap == cap::null || root_dir_cap == cap::error) {
            return cap::error;
        }

        CapIdx child_root_cap = sys_cap_clone(root_dir_cap);
        if (child_root_cap == cap::null || child_root_cap == cap::error) {
            return cap::error;
        }

        struct RootDirBootstrap {
            BootstrapRecordHeader header;
            CapIdx cap;
            char path[2];
        } bootstrap{
            .header = BootstrapRecordHeader{
                .next = 0,
                .type = BOOTSTRAP_TYPE_DIRCAPEXPLAIN,
            },
            .cap  = child_root_cap,
            .path = "/",
        };

        CapIdx initial_caps[] = {child_root_cap};
        CapIdx child_pcb = sys_create_process(kmod_getcap(fd), initial_caps, 1,
                                              sched_class, &bootstrap,
                                              sizeof(bootstrap));
        sys_cap_remove(child_root_cap);
        return child_pcb;
    }
}  // namespace

int kmod_main() {
    printf("进入 init 模块!\n");
    CapIdx root_dir_cap = bootstrap_root_dir();
    if (root_dir_cap == cap::null || root_dir_cap == cap::error) {
        printf("init: bootstrap root dir capability missing\n");
        exit(-1);
    }

    int fd = kmod_fopen("/initrd/test_fork.mod", "x");
    if (fd >= 0) {
        if (spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap) ==
            cap::error)
        {
            printf("init: create test_fork failed\n");
        }
        kmod_fclose(fd);
    }

    fd = kmod_fopen("/initrd/test_thread.mod", "x");
    if (fd >= 0) {
        if (spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap) ==
            cap::error)
        {
            printf("init: create test_thread failed\n");
        }
        kmod_fclose(fd);
    }

    fd = kmod_fopen("/initrd/test_endpoint_master.mod", "x");
    if (fd >= 0) {
        if (spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap) ==
            cap::error)
        {
            printf("init: create test_endpoint_master failed\n");
        }
        kmod_fclose(fd);
    }

    fd = kmod_fopen("/initrd/test_call_service.mod", "x");
    if (fd >= 0) {
        if (spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap) ==
            cap::error)
        {
            printf("init: create test_call_service failed\n");
        }
        kmod_fclose(fd);
    }

    fd = kmod_fopen("/initrd/test_rpc_server.mod", "x");
    if (fd >= 0) {
        if (spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap) ==
            cap::error)
        {
            printf("init: create test_rpc_server failed\n");
        }
        kmod_fclose(fd);
    }

    fd = kmod_fopen("/initrd/test_file_rw_a.mod", "x");
    if (fd >= 0) {
        if (spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap) ==
            cap::error)
        {
            printf("init: create test_file_rw_a failed\n");
        }
        kmod_fclose(fd);
    }

    printf("init: 启动完成, 退出\n");
    exit(0);
    return 0;
}
