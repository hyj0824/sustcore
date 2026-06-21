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

int mark_cnt = 0;

namespace {
    constexpr size_t kGetdentsBufferSize = 2048;
    constexpr size_t kMaxPrintDepth      = 4;

    // 在 bootstrap 信息中寻找根目录能力
    [[nodiscard]]
    CapIdx bootstrap_root_dir() {
        CapIdx cap = cap::null;
        bool found = false;
        bool ok    = bootstrap_foreach_record(
            __bsargv, __bsargc,
            [&](const BootstrapRecordView &view) {
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
    // 返回值: 新进程的 pid
    constexpr size_t INVALID_PID = 0xFFFFFFFFFFFFFFFF;

    [[nodiscard]]
    size_t spawn_with_root_dir(int fd, size_t sched_class,
                               CapIdx root_dir_cap) {
        if (fd < 0 || root_dir_cap == cap::null || root_dir_cap == cap::error) {
            return INVALID_PID;
        }

        CapIdx child_root_cap = sys_cap_clone(root_dir_cap);
        if (child_root_cap == cap::null || child_root_cap == cap::error) {
            return INVALID_PID;
        }

        struct RootDirBootstrap {
            bsheader header;
            CapIdx cap;
            char path[2];
        } bootstrap{
            .header = bsheader{
                .size = sizeof(RootDirBootstrap),
                .type = BOOTSTRAP_TYPE_DIRCAPEXPLAIN,
            },
            .cap  = child_root_cap,
            .path = "/",
        };

        CapIdx initial_caps[] = {child_root_cap, cap::null};
        const char *bsargv[]  = {reinterpret_cast<const char *>(&bootstrap),
                                 nullptr};
        CapIdx child_pcb =
            sys_create_process(kmod_getcap(fd), sched_class, initial_caps,
                               nullptr, nullptr, bsargv);
        sys_cap_remove(child_root_cap);

        if (child_pcb != cap::error) {
            size_t pid = sys_getpid(child_pcb);
            return pid;
        }
        return INVALID_PID;
    }

    [[nodiscard]]
    size_t spawn_linux_with_root_dir(int fd, size_t sched_class,
                                     CapIdx root_dir_cap) {
        if (fd < 0 || root_dir_cap == cap::null || root_dir_cap == cap::error) {
            return INVALID_PID;
        }

        CapIdx child_root_cap = sys_cap_clone(root_dir_cap);
        if (child_root_cap == cap::null || child_root_cap == cap::error) {
            return INVALID_PID;
        }

        struct RootDirBootstrap {
            bsheader header;
            CapIdx cap;
            char path[2];
        } bootstrap{
            .header = bsheader{
                .size = sizeof(RootDirBootstrap),
                .type = BOOTSTRAP_TYPE_DIRCAPEXPLAIN,
            },
            .cap  = child_root_cap,
            .path = "/",
        };

        CapIdx initial_caps[] = {child_root_cap, cap::null};
        const char *bsargv[]  = {reinterpret_cast<const char *>(&bootstrap),
                                 nullptr};
        CapIdx child_pcb = sys_create_linux_process(
            kmod_getcap(fd), sched_class, initial_caps, nullptr, nullptr,
            bsargv);
        sys_cap_remove(child_root_cap);

        if (child_pcb != cap::error) {
            return sys_getpid(child_pcb);
        }
        return INVALID_PID;
    }

    void print_indent(size_t depth) {
        for (size_t i = 0; i < depth; ++i) {
            printf("  ");
        }
    }

    [[nodiscard]]
    const char *find_name_end(const char *name, size_t len) {
        return static_cast<const char *>(memchr(name, '\0', len));
    }

    void print_tree(CapIdx dir_cap, const char *path, size_t depth = 0) {
        if (dir_cap == cap::null || dir_cap == cap::error || path == nullptr) {
            return;
        }

        char buffer[kGetdentsBufferSize];
        size_t doff = 0;
        while (doff != DIR_ENTRY_END) {
            size_t bytes_written =
                sys_vfs_getdents(dir_cap, buffer, sizeof(buffer), doff);

            if (bytes_written == 0) {
                break;
            }

            size_t parsed_entries = 0;
            bool reached_end      = false;
            bool batch_valid      = true;

            for (size_t offset = 0; offset < bytes_written;) {
                if (bytes_written - offset < sizeof(dir_entry_header)) {
                    batch_valid = false;
                    break;
                }

                auto *header =
                    reinterpret_cast<dir_entry_header *>(&buffer[offset]);
                size_t name_offset = offset + sizeof(dir_entry_header);
                size_t name_len    = bytes_written - name_offset;
                const char *name   = &buffer[name_offset];
                if (find_name_end(name, name_len) == nullptr || name[0] == '\0')
                {
                    batch_valid = false;
                    break;
                }

                char child_path[512]{};
                if (strcmp(path, "/") == 0) {
                    snprintf(child_path, sizeof(child_path), "/%s", name);
                } else {
                    snprintf(child_path, sizeof(child_path), "%s/%s", path,
                             name);
                }

                NodeMeta st {};
                bool stat_ok = sys_vfs_lstat(dir_cap, name, &st);
                const bool is_dir = stat_ok && st.type == EntryType::DIR;
                const char *kind =
                    !stat_ok ? "UNK "
                             : (st.type == EntryType::DIR
                                    ? "DIR "
                                    : (st.type == EntryType::SYMLINK
                                           ? "LNK "
                                           : "FILE"));
                char link_target[256]{};
                bool has_link_target = false;
                if (stat_ok && st.type == EntryType::SYMLINK) {
                    size_t got =
                        sys_vfs_readlink(dir_cap, name, link_target,
                                         sizeof(link_target) - 1);
                    if (got < sizeof(link_target)) {
                        link_target[got] = '\0';
                        has_link_target = true;
                    }
                }
                print_indent(depth);
                if (has_link_target) {
                    printf("%s %s -> %s\n", kind, child_path, link_target);
                } else {
                    printf("%s %s\n", kind, child_path);
                }

                if (is_dir && depth + 1 < kMaxPrintDepth) {
                    CapIdx subdir =
                        sys_vfs_opendir(dir_cap, name, flags::O_READ);
                    if (subdir != cap::null && subdir != cap::error) {
                        print_tree(subdir, child_path, depth + 1);
                        sys_cap_remove(subdir);
                    }
                }
                ++parsed_entries;

                if (header->next_offset == DIR_ENTRY_END) {
                    reached_end = true;
                    break;
                }
                if (header->next_offset == 0 ||
                    header->next_offset < sizeof(dir_entry_header) ||
                    offset + header->next_offset > bytes_written)
                {
                    break;
                }
                offset += header->next_offset;
            }

            if (parsed_entries == 0 || !batch_valid) {
                break;
            }
            if (reached_end) {
                doff = DIR_ENTRY_END;
                break;
            }
            doff += parsed_entries;
        }
    }
}  // namespace

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
                         const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;
    mark_cnt = 0;
    printf("进入 init 模块!\n");

    CapIdx root_dir_cap = bootstrap_root_dir();
    // if (root_dir_cap == cap::null || root_dir_cap == cap::error) {
    //     printf("init: bootstrap root dir capability missing\n");
    //     exit(-1);
    // }

    int fd = 0;
    fd     = kmod_fopen("/initrd/test_fork.mod", "x");
    if (fd >= 0) {
        size_t pid = spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
        if (pid == INVALID_PID) {
            printf("init: create test_fork failed\n");
        } else {
            printf("init: create test_fork pid=%lu\n",
                   static_cast<unsigned long>(pid));
        }
        kmod_fclose(fd);
    }

    // fd = kmod_fopen("/initrd/test_thread.mod", "x");
    // if (fd >= 0) {
    //     size_t pid = spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
    //     if (pid == INVALID_PID) {
    //         printf("init: create test_thread failed\n");
    //     } else {
    //         printf("init: create test_thread pid=%lu\n",
    //                static_cast<unsigned long>(pid));
    //     }
    //     kmod_fclose(fd);
    // }

    // fd = kmod_fopen("/initrd/test_endpoint_master.mod", "x");
    // if (fd >= 0) {
    //     size_t pid = spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
    //     if (pid == INVALID_PID) {
    //         printf("init: create test_endpoint_master failed\n");
    //     } else {
    //         printf("init: create test_endpoint_master pid=%lu\n",
    //                static_cast<unsigned long>(pid));
    //     }
    //     kmod_fclose(fd);
    // }

    fd = kmod_fopen("/initrd/test_call_service.mod", "x");
    if (fd >= 0) {
        size_t pid = spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
        if (pid == INVALID_PID) {
            printf("init: create test_call_service failed\n");
        } else {
            printf("init: create test_call_service pid=%lu\n",
                   static_cast<unsigned long>(pid));
        }
        kmod_fclose(fd);
    }

    // fd = kmod_fopen("/initrd/test_rpc_server.mod", "x");
    // if (fd >= 0) {
    //     size_t pid = spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
    //     if (pid == INVALID_PID) {
    //         printf("init: create test_rpc_server failed\n");
    //     } else {
    //         printf("init: create test_rpc_server pid=%lu\n",
    //                static_cast<unsigned long>(pid));
    //     }
    //     kmod_fclose(fd);
    // }

    // fd = kmod_fopen("/initrd/test_file_rw_a.mod", "x");
    // if (fd >= 0) {
    //     size_t pid = spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
    //     if (pid == INVALID_PID) {
    //         printf("init: create test_file_rw_a failed\n");
    //     } else {
    //         printf("init: create test_file_rw_a pid=%lu\n",
    //                static_cast<unsigned long>(pid));
    //     }
    //     kmod_fclose(fd);
    // }

    if (kmod_symlink("/lib", "/initrd/tmp/lib/") < 0) {
        printf("init: create /lib symlink failed\n");
    }
    printf ("link /lib/ -> /initrd/tmp/lib/ created\n");

    fd = kmod_fopen("/initrd/test-linux.mod", "x");
    if (fd >= 0) {
        size_t pid =
            spawn_linux_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
        if (pid == INVALID_PID) {
            printf("init: create test-linux failed\n");
        } else {
            printf("init: create test-linux pid=%lu\n",
                   static_cast<unsigned long>(pid));
        }
        kmod_fclose(fd);
    }

    fd = kmod_fopen("/initrd/tmp/write", "x");
    if (fd >= 0) {
        size_t pid =
            spawn_linux_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
        if (pid == INVALID_PID) {
            printf("init: create test-linux failed\n");
        } else {
            printf("init: create test-linux pid=%lu\n",
                   static_cast<unsigned long>(pid));
        }
        kmod_fclose(fd);
    }

    // ext4 score tests — run sequentially after rw test
    // fd = kmod_fopen("/initrd/test_fs_basic.mod", "x");
    // if (fd >= 0) {
    //     if (spawn_with_root_dir(fd, SCHED_CLASS_FCFS, root_dir_cap) ==
    //         cap::error)
    //     {
    //         printf("init: create test_fs_basic failed\n");
    //     } else {
    //         printf("init: fs basic test spawned\n");
    //     }
    //     kmod_fclose(fd);
    // }

    // try write file /sys/dev/serial@10000000/serial
    // fd = kmod_fopen("/sys/dev/serial@10000000/serial", "w");
    // if (fd >= 0) {
    //     kmod_fwrite(fd, "Hello, World!\n", 14);
    //     printf(
    //         "init: write \"Hello, World!\" to "
    //         "/sys/dev/serial@10000000/serial\n");
    //     kmod_fclose(fd);
    // } else {
    //     printf("init: can't open `/sys/dev/serial@10000000/serial` !\n");
    // }

    printf("init: 打印目录树\n");
    print_tree(root_dir_cap, "/");

    printf("init: 启动完成, 退出\n");
    exit(0);
    return 0;
}
