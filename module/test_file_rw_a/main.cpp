/**
 * @file main.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 文件系统测试 A
 * @version alpha-1.0.0
 * @date 2026-06-11
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include <sustcore/bootstrap.h>
#include <kmod/syscall.h>

#include <cstdio>
#include <cstring>

namespace {
    constexpr const char *TMPFS_DIR0  = "/tmpfs/";
    constexpr const char *TMPFS_DIR1  = "/tmpfs/abc/";
    constexpr const char *TMPFS_FILE  = "/tmpfs/abc/file1.txt";
    constexpr const char *MODULE_B    = "/initrd/test_file_rw_b.mod";
    constexpr const char *TEST_TEXT =
        "Hello, TmpFS! This is the test text from test_file_rw_a!\n"
        "你好TmpFS, 这是来自 test_file_rw_a 进程的测试文本!";

    [[nodiscard]]
    CapIdx bootstrap_root_dir() {
        CapIdx cap = cap::null;
        bool found = false;
        bool ok    = bootstrap_foreach_record(
            __bsargv, __bsargc,
            [&](const BootstrapRecordView &view) {
                if (found || view.header->type != boot::TYPE_CAPEXP)
                {
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
}

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv_in[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv_in;
    printf("test_file_rw_a: start pid=%u\n", sys_getpid(__pcb_cap).value());

    if (kmod_mkdir(TMPFS_DIR0) != 0) {
        printf("test_file_rw_a: mkdir failed: %s\n", TMPFS_DIR0);
        exit(-1);
    }
    printf("test_file_rw_a: mkdir ok: %s\n", TMPFS_DIR0);

    if (kmod_mkdir(TMPFS_DIR1) != 0) {
        printf("test_file_rw_a: mkdir failed: %s\n", TMPFS_DIR1);
        exit(-1);
    }
    printf("test_file_rw_a: mkdir ok: %s\n", TMPFS_DIR1);

    int fd = kmod_mkfile(TMPFS_FILE, "w+");
    if (fd < 0) {
        printf("test_file_rw_a: mkfile failed: %s\n", TMPFS_FILE);
        exit(-1);
    }
    printf("test_file_rw_a: mkfile ok: %s\n", TMPFS_FILE);

    const size_t text_len = strlen(TEST_TEXT);
    size_t written        = kmod_fwrite(fd, TEST_TEXT, text_len);
    if (written != text_len) {
        printf("test_file_rw_a: fwrite failed, expect=%u actual=%u\n",
               static_cast<unsigned>(text_len), static_cast<unsigned>(written));
        kmod_fclose(fd);
        exit(-1);
    }
    kmod_fclose(fd);
    printf("test_file_rw_a: write ok, len=%u\n",
           static_cast<unsigned>(text_len));

    int exec_fd = kmod_fopen(MODULE_B, "x");
    if (exec_fd < 0) {
        printf("test_file_rw_a: open module B failed: %s\n", MODULE_B);
        exit(-1);
    }

    CapIdx root_dir_cap = bootstrap_root_dir();
    if (root_dir_cap == cap::null || root_dir_cap == cap::error) {
        printf("test_file_rw_a: bootstrap root dir missing\n");
        kmod_fclose(exec_fd);
        exit(-1);
    }

    CapIdx reserved_caps[] = {root_dir_cap, cap::null};
    struct RootDirBootstrap {
        bsheader header;
        BootstrapCapExplainPayloadHead explain;
        char desc[3];
    } bootstrap{
        .header = bsheader{
            .size = sizeof(RootDirBootstrap),
            .type = boot::TYPE_CAPEXP,
        },
        .explain =
            BootstrapCapExplainPayloadHead{
                .cap_idx  = root_dir_cap,
                .cap_type = PayloadType::VDIR,
                .cap_perm = ~b64(0),
            },
        .desc = "#/",
    };
    const char *bsargv[] = {reinterpret_cast<const char *>(&bootstrap),
                            nullptr};

    printf("test_file_rw_a: execve -> %s\n", MODULE_B);
    ExecveRequest request{
        .image_cap = kmod_getcap(exec_fd),
        .execfn    = MODULE_B,
        .caps      = reserved_caps,
        .argv      = nullptr,
        .envp      = nullptr,
        .bsargv    = bsargv,
    };
    if (!execve(&request)
             .to_result()
             .has_value())
    {
        printf("test_file_rw_a: execve failed\n");
        kmod_fclose(exec_fd);
        exit(-1);
    }

    kmod_fclose(exec_fd);
    printf("test_file_rw_a: unexpected return after execve\n");
    exit(-1);
    return 0;
}
