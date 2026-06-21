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

#include <kmod/syscall.h>
#include <sustcore/bootstrap.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int mark_cnt = 0;

constexpr size_t kGetdentsBufferSize = 2048;
constexpr size_t kMaxPrintDepth      = 4;

// 在 bootstrap 信息中寻找根目录能力
[[nodiscard]]
CapIdx bootstrap_root_dir() {
    CapIdx cap = cap::null;
    bool found = false;
    bool ok    = bootstrap_foreach_record(
        __bsargv, __bsargc, [&](const BootstrapRecordView &view) {
            if (found || view.header->type != boot::TYPE_CAPEXP) {
                return;
            }
            BootstrapCapExplainView cap_explain{};
            if (!bootstrap_parse_cap_explain(view, cap_explain) ||
                cap_explain.cap_type != PayloadType::VDIR ||
                cap_explain.cap_desc == nullptr ||
                cap_explain.cap_desc[0] != '#')
            {
                return;
            }
            if (strcmp(cap_explain.cap_desc + 1, "/") != 0) {
                return;
            }
            cap   = cap_explain.cap_idx;
            found = true;
        });
    return ok && found ? cap : cap::null;
}

// 创建新进程并传递根目录能力
// 返回值: 新进程的 pid
constexpr size_t INVALID_PID = 0xFFFFFFFFFFFFFFFF;

[[nodiscard]]
size_t spawn_with_root_dir(int fd, size_t sched_class, CapIdx root_dir_cap) {
    if (fd < 0 || root_dir_cap == cap::null || root_dir_cap == cap::error) {
        return INVALID_PID;
    }

    CapIdx child_root_cap = sys_cap_clone(root_dir_cap);
    if (child_root_cap == cap::null || child_root_cap == cap::error) {
        return INVALID_PID;
    }

    struct RootDirBootstrap {
        bsheader header;
        BootstrapCapExplainPayloadHead explain;
        char desc[3];
    } bootstrap{
        .header =
            bsheader{
                .size = sizeof(RootDirBootstrap),
                .type = boot::TYPE_CAPEXP,
            },
        .explain =
            BootstrapCapExplainPayloadHead{
                .cap_idx  = child_root_cap,
                .cap_type = PayloadType::VDIR,
                .cap_perm = ~b64(0),
            },
        .desc = "#/",
    };

    CapIdx initial_caps[] = {child_root_cap, cap::null};
    const char *bsargv[]  = {reinterpret_cast<const char *>(&bootstrap),
                             nullptr};
    CapIdx child_pcb      = sys_create_process(
        kmod_getcap(fd), sched_class, initial_caps, nullptr, nullptr, bsargv);
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
        BootstrapCapExplainPayloadHead explain;
        char desc[3];
    } bootstrap{
        .header =
            bsheader{
                .size = sizeof(RootDirBootstrap),
                .type = boot::TYPE_CAPEXP,
            },
        .explain =
            BootstrapCapExplainPayloadHead{
                .cap_idx  = child_root_cap,
                .cap_type = PayloadType::VDIR,
                .cap_perm = ~b64(0),
            },
        .desc = "#/",
    };

    CapIdx initial_caps[] = {child_root_cap, cap::null};
    const char *bsargv[]  = {reinterpret_cast<const char *>(&bootstrap),
                             nullptr};
    CapIdx child_pcb      = sys_create_linux_process(
        kmod_getcap(fd), sched_class, initial_caps, nullptr, nullptr, bsargv);
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

bool force_symlink(const char *path, const char *target) {
    if (path == nullptr || target == nullptr || path[0] == '\0' ||
        target[0] == '\0')
    {
        return false;
    }
    (void)kmod_unlink(path);
    return kmod_symlink(path, target) == 0;
}

std::vector<std::string> get_alldents(CapIdx dir_cap) {
    std::vector<std::string> entries{};
    if (dir_cap == cap::null || dir_cap == cap::error) {
        return entries;
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
            auto *name_end     = find_name_end(name, name_len);
            if (name_end == nullptr || name[0] == '\0') {
                batch_valid = false;
                break;
            }

            entries.emplace_back(name);
            ++parsed_entries;

            if (header->next_offset == DIR_ENTRY_END) {
                reached_end = true;
                break;
            }
            if (header->next_offset == 0 ||
                header->next_offset < sizeof(dir_entry_header) ||
                offset + header->next_offset > bytes_written)
            {
                batch_valid = false;
                break;
            }
            offset += header->next_offset;
        }

        if (parsed_entries == 0 || !batch_valid) {
            break;
        }
        if (reached_end) {
            break;
        }
        doff += parsed_entries;
    }
    return entries;
}

void print_tree(CapIdx dir_cap, const char *path, size_t depth = 0) {
    if (dir_cap == cap::null || dir_cap == cap::error || path == nullptr) {
        return;
    }

    auto names = get_alldents(dir_cap);
    for (const auto &name : names) {
        char child_path[512]{};
        if (strcmp(path, "/") == 0) {
            snprintf(child_path, sizeof(child_path), "/%s", name.c_str());
        } else {
            snprintf(child_path, sizeof(child_path), "%s/%s", path,
                     name.c_str());
        }

        NodeMeta st{};
        bool stat_ok      = sys_vfs_lstat(dir_cap, name.c_str(), &st);
        const bool is_dir = stat_ok && st.type == EntryType::DIR;
        const char *kind =
            !stat_ok
                ? "UNK "
                : (st.type == EntryType::DIR
                       ? "DIR "
                       : (st.type == EntryType::SYMLINK ? "LNK " : "FILE"));
        char link_target[256]{};
        bool has_link_target = false;
        if (stat_ok && st.type == EntryType::SYMLINK) {
            size_t got = sys_vfs_readlink(dir_cap, name.c_str(), link_target,
                                          sizeof(link_target) - 1);
            if (got < sizeof(link_target)) {
                link_target[got] = '\0';
                has_link_target  = true;
            }
        }
        print_indent(depth);
        if (has_link_target) {
            printf("%s %s -> %s\n", kind, child_path, link_target);
        } else {
            printf("%s %s\n", kind, child_path);
        }

        if (is_dir && depth + 1 < kMaxPrintDepth) {
            CapIdx subdir = sys_vfs_opendir(dir_cap, name.c_str(), flags::O_READ);
            if (subdir != cap::null && subdir != cap::error) {
                print_tree(subdir, child_path, depth + 1);
                sys_cap_remove(subdir);
            }
        }
    }
}

void create_blk_linkings(CapIdx root_dir_cap) {
    if (root_dir_cap == cap::null || root_dir_cap == cap::error) {
        printf("init: bootstrap root dir capability missing\n");
        return;
    }

    CapIdx dev_dir =
        sys_vfs_mkdir(root_dir_cap, "dev/",
                      flags::O_READ | flags::O_WRITE | flags::O_EXECUTE);
    if (dev_dir != cap::null && dev_dir != cap::error) {
        sys_cap_remove(dev_dir);
    }

    CapIdx sysdev_cap = sys_vfs_opendir(root_dir_cap, "sys/dev", flags::O_READ);
    if (sysdev_cap == cap::null || sysdev_cap == cap::error) {
        printf("init: open /sys/dev failed\n");
        return;
    }

    const char *sd_paths[] = {"/dev/sda", "/dev/sdb", "/dev/sdc", "/dev/sdd"};
    auto dev_names         = get_alldents(sysdev_cap);
    size_t blk_count       = 0;

    for (const auto &device_name : dev_names) {
        if (blk_count >= 4) {
            break;
        }

        CapIdx device_dir =
            sys_vfs_opendir(sysdev_cap, device_name.c_str(), flags::O_READ);
        if (device_dir == cap::null || device_dir == cap::error) {
            continue;
        }

        auto child_names = get_alldents(device_dir);
        bool has_blk     = false;
        for (const auto &child_name : child_names) {
            if (child_name == "blk") {
                has_blk = true;
                break;
            }
        }
        sys_cap_remove(device_dir);
        if (!has_blk) {
            continue;
        }

        char source_path[512]{};
        snprintf(source_path, sizeof(source_path), "/sys/dev/%s/blk",
                 device_name.c_str());
        if (force_symlink(sd_paths[blk_count], source_path)) {
            printf("init: link %s -> %s\n", sd_paths[blk_count], source_path);
        } else {
            printf("init: create %s failed\n", sd_paths[blk_count]);
        }
        ++blk_count;
    }

    sys_cap_remove(sysdev_cap);
}

void mount_testing_ext4(CapIdx root_dir_cap) {
    if (root_dir_cap == cap::null || root_dir_cap == cap::error) {
        printf("init: bootstrap root dir capability missing\n");
        return;
    }

    CapIdx blk_cap = sys_vfs_open(root_dir_cap, "dev/sda", flags::O_READ);
    if (blk_cap == cap::null || blk_cap == cap::error) {
        printf("init: open /dev/sda failed\n");
        return;
    }

    CapIdx testing_dir =
        sys_vfs_mkdir(root_dir_cap, "testing/",
                      flags::O_READ | flags::O_WRITE | flags::O_EXECUTE);
    if (testing_dir != cap::null && testing_dir != cap::error) {
        sys_cap_remove(testing_dir);
    }

    bool mounted =
        sys_vfs_mount(root_dir_cap, "ext4", blk_cap, "testing/", 0, nullptr);
    if (mounted) {
        printf("init: mount /testing succeeded\n");
    } else {
        printf("init: mount /testing failed\n");
    }
    sys_cap_remove(blk_cap);
}

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
    // fd     = kmod_fopen("/initrd/test_fork.mod", "x");
    // if (fd >= 0) {
    //     size_t pid = spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
    //     if (pid == INVALID_PID) {
    //         printf("init: create test_fork failed\n");
    //     } else {
    //         printf("init: create test_fork pid=%lu\n",
    //                static_cast<unsigned long>(pid));
    //     }
    //     kmod_fclose(fd);
    // }
    // else {
    //     printf("init: /initrd/test_fork.mod not found, skipping fork
    //     test\n");
    // }

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

    // fd = kmod_fopen("/initrd/test_call_service.mod", "x");
    // if (fd >= 0) {
    //     size_t pid = spawn_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
    //     if (pid == INVALID_PID) {
    //         printf("init: create test_call_service failed\n");
    //     } else {
    //         printf("init: create test_call_service pid=%lu\n",
    //                static_cast<unsigned long>(pid));
    //     }
    //     kmod_fclose(fd);
    // }
    // else {
    //     printf("init: /initrd/test_fork.mod not found, skipping fork
    //     test\n");
    // }

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

#if defined(__ARCH_riscv64__)

#define LIB_PATH "/lib"

#elif defined(__ARCH_loongarch64__)

#define LIB_PATH "/lib64"

#endif

    if (!force_symlink(LIB_PATH, "/initrd/tmp/lib")) {
        printf("init: create /lib symlink failed\n");
    } else {
        printf("link " LIB_PATH " -> /initrd/tmp/lib created\n");
    }

    // fd = kmod_fopen("/initrd/test-linux.mod", "x");
    // if (fd >= 0) {
    //     size_t pid =
    //         spawn_linux_with_root_dir(fd, SCHED_CLASS_RR, root_dir_cap);
    //     if (pid == INVALID_PID) {
    //         printf("init: create test-linux failed\n");
    //     } else {
    //         printf("init: create test-linux pid=%lu\n",
    //                static_cast<unsigned long>(pid));
    //     }
    //     kmod_fclose(fd);
    // }
    // else {
    //     printf("init: /initrd/test-linux.mod not found, skipping linux
    //     test\n");
    // }

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
    } else {
        printf("init: /initrd/tmp/write not found, skipping write test\n");
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

    create_blk_linkings(root_dir_cap);
    mount_testing_ext4(root_dir_cap);
    printf("init: 打印目录树\n");
    print_tree(root_dir_cap, "/");

    printf("init: 启动完成, 退出\n");
    exit(0);
    return 0;
}
