/**
 * @file main.cpp
 * @brief Combined fs_score tests: basic → rw → dir → names → holes → errors → stress
 */
#include <sustcore/bootstrap.h>
#include <kmod/syscall.h>

#include <cstdio>
#include <cstring>

namespace {
    char g_buf[16384];
    char g_read[16384];

    [[nodiscard]]
    CapIdx bootstrap_root_dir() {
        CapIdx cap = cap::null;
        bool found = false;
        bool ok = bootstrap_foreach_record(
            __bsargv, __bsargc,
            [&](const BootstrapRecordView &view) {
                if (found || view.header->type != boot::TYPE_CAPEXP)
                    return;
                BootstrapCapExplainView cap_explain{};
                if (!bootstrap_parse_cap_explain(view, cap_explain) ||
                    cap_explain.cap_type != PayloadType::VDIR ||
                    cap_explain.cap_desc == nullptr ||
                    cap_explain.cap_desc[0] != '#')
                    return;
                if (strcmp(cap_explain.cap_desc + 1, "/") != 0) return;
                cap = cap_explain.cap_idx;
                found = true;
            });
        return ok && found ? cap : cap::null;
    }

    void fail(const char *name, const char *msg) {
        printf(" FAIL %s: %s\n", name, msg);
        exit(-1);
    }
    void check(const char *name, bool cond, const char *msg) {
        if (!cond) fail(name, msg);
    }
    void pass(const char *name) {
        printf(" PASS %s\n", name);
    }

    // ---- basic ----
    void test_basic(CapIdx root_cap) {
        const char *N = "basic";
        const char *P = "/test_img/t_basic";

        int fd = kmod_mkfile(P, "w+");
        check(N, fd >= 0, "create failed");

        size_t w = kmod_fwrite(fd, "abcdef", 6);
        check(N, w == 6, "write 6 failed");
        kmod_fclose(fd);

        fd = kmod_fopen(P, "r");
        check(N, fd >= 0, "reopen failed");
        memset(g_read, 0, sizeof(g_read));
        size_t r = kmod_fread(fd, g_read, 6);
        kmod_fclose(fd);
        check(N, r == 6 && memcmp(g_read, "abcdef", 6) == 0, "read mismatch");

        kmod_unlink(P);
        fd = kmod_mkfile(P, "w+");
        w = kmod_fwrite(fd, "ok", 2);
        check(N, w == 2, "overwrite failed");
        kmod_fclose(fd);

        fd = kmod_fopen(P, "r");
        memset(g_read, 0, 10);
        r = kmod_fread(fd, g_read, 10);
        kmod_fclose(fd);
        check(N, r == 2 && memcmp(g_read, "ok", 2) == 0, "overwrite mismatch");

        check(N, kmod_unlink(P) == 0, "unlink failed");
        check(N, kmod_unlink(P) < 0, "double unlink should fail");
        pass(N);
    }

    // ---- rw ----
    void test_rw(CapIdx root_cap) {
        const char *N = "rw";
        const char *P = "/test_img/t_rw";

        int fd = kmod_mkfile(P, "w+");
        check(N, fd >= 0, "create failed");

        constexpr size_t kSz = 8192;
        for (size_t i = 0; i < kSz; ++i)
            g_buf[i] = static_cast<char>((i * 31 + 17) & 0xFF);
        size_t w = kmod_fwrite(fd, g_buf, kSz);
        check(N, w == kSz, "write 8k failed");
        kmod_fclose(fd);

        fd = kmod_fopen(P, "r");
        memset(g_read, 0, kSz);
        size_t r = kmod_fread(fd, g_read, kSz);
        kmod_fclose(fd);
        check(N, r == kSz && memcmp(g_read, g_buf, kSz) == 0, "data mismatch");

        // read past size within same fd
        fd = kmod_fopen(P, "r");
        r = kmod_fread(fd, g_read, kSz + 100);
        check(N, r == kSz, "read beyond size should cap at size");
        kmod_fclose(fd);

        kmod_unlink(P);
        pass(N);
    }

    // ---- dir ----
    void test_dir(CapIdx root_cap) {
        const char *N = "dir";
        const char *D = "/test_img/t_dir";

        int ret = kmod_mkdir(D);
        check(N, ret == 0, "mkdir failed");

        CapIdx dir = sys_vfs_opendir(root_cap, "test_img", flags::O_READ).value();
        check(N, dir != cap::null && dir != cap::error, "opendir failed");

        // check entry exists
        bool found = false;
        char dname[256] = {};
        size_t doff = 0;
        char dent[16384];
        while (doff != DIR_ENTRY_END) {
            size_t bytes =
                sys_vfs_getdents(dir, dent, sizeof(dent), doff).value();
            if (bytes == 0) break;
            for (size_t off = 0; off < bytes;) {
                auto *h = reinterpret_cast<const dir_entry_header *>(dent + off);
                const char *nm = dent + off + sizeof(dir_entry_header);
                size_t nl = strlen(nm);
                if (nm[0] == 't' && nm[1] == '_' && nm[2] == 'd' &&
                    nm[3] == 'i' && nm[4] == 'r' && nl == 5) {
                    found = true;
                    NodeMeta st {};
                    check(N, sys_vfs_stat(dir, nm, &st),
                          "stat t_dir failed");
                    check(N, st.type == EntryType::DIR, "t_dir should be dir");
                    break;
                }
                off += h->next_offset;
            }
            doff += bytes;
            if (found) break;
        }
        (void)sys_cap_remove(dir).to_result();
        check(N, found, "t_dir not in dir");

        // create file inside dir, verify
        int fd = kmod_mkfile("/test_img/t_dir/sub", "w+");
        check(N, fd >= 0, "create sub file failed");
        kmod_fwrite(fd, "data", 4);
        kmod_fclose(fd);

        fd = kmod_fopen("/test_img/t_dir/sub", "r");
        memset(g_read, 0, 10);
        size_t r = kmod_fread(fd, g_read, 10);
        kmod_fclose(fd);
        check(N, r == 4 && memcmp(g_read, "data", 4) == 0, "sub file mismatch");

        kmod_unlink("/test_img/t_dir/sub");
        check(N, kmod_rmdir(D) == 0, "rmdir failed");
        pass(N);
    }

    // ---- names ----
    void test_names(CapIdx root_cap) {
        const char *N = "names";
        const char *names[] = {
            "/test_img/t_abc",
            "/test_img/t_123",
            "/test_img/t_AbC",
            "/test_img/t__under",
            "/test_img/t_dotdot",
            "/test_img/t_loooooooooooooong_name_123456789",
            nullptr
        };
        for (int i = 0; names[i]; ++i) {
            int fd = kmod_mkfile(names[i], "w+");
            check(N, fd >= 0, names[i]);
            kmod_fwrite(fd, "X", 1);
            kmod_fclose(fd);

            fd = kmod_fopen(names[i], "r");
            check(N, fd >= 0, names[i]);
            size_t r = kmod_fread(fd, g_read, 1);
            kmod_fclose(fd);
            check(N, r == 1 && g_read[0] == 'X', names[i]);
            kmod_unlink(names[i]);
        }
        pass(N);
    }

    // ---- stress ----
    void test_stress(CapIdx root_cap) {
        const char *N = "stress";
        constexpr int kFiles = 30;
        char path[64];
        for (int i = 0; i < kFiles; ++i) {
            snprintf(path, sizeof(path), "/test_img/ts_%d", i);
            int fd = kmod_mkfile(path, "w+");
            check(N, fd >= 0, path);
            char v = static_cast<char>('A' + (i % 26));
            kmod_fwrite(fd, &v, 1);
            kmod_fclose(fd);
        }
        for (int i = 0; i < kFiles; ++i) {
            snprintf(path, sizeof(path), "/test_img/ts_%d", i);
            int fd = kmod_fopen(path, "r");
            check(N, fd >= 0, path);
            size_t r = kmod_fread(fd, g_read, 1);
            kmod_fclose(fd);
            char expect = static_cast<char>('A' + (i % 26));
            check(N, r == 1 && g_read[0] == expect, "stress data mismatch");
        }
        for (int i = 0; i < kFiles; ++i) {
            snprintf(path, sizeof(path), "/test_img/ts_%d", i);
            kmod_unlink(path);
        }
        pass(N);
    }

    // ---- errors ----
    void test_errors(CapIdx root_cap) {
        const char *N = "errors";

        // missing file open should fail
        int fd = kmod_fopen("/test_img/no_such_file", "r");
        check(N, fd < 0, "open missing file should fail");

        // missing file unlink should fail
        check(N, kmod_unlink("/test_img/no_such_file") < 0, "unlink missing should fail");

        // mkdir duplicate
        check(N, kmod_mkdir("/test_img/t_err_dir") == 0, "mkdir failed");
        check(N, kmod_mkdir("/test_img/t_err_dir") < 0, "duplicate mkdir should fail");

        // create file, then try rmdir on it (not a dir)
        kmod_unlink("/test_img/t_err_file");
        fd = kmod_mkfile("/test_img/t_err_file", "w+");
        check(N, fd >= 0, "create file failed");
        kmod_fwrite(fd, "x", 1);
        kmod_fclose(fd);
        check(N, kmod_rmdir("/test_img/t_err_file") < 0, "rmdir file should fail");

        // mkdir under a regular file path should fail
        check(N, kmod_mkdir("/test_img/t_err_file/sub") < 0, "mkdir under file should fail");

        // unlink dir should fail
        check(N, kmod_unlink("/test_img/t_err_dir") < 0, "unlink dir should fail");

        kmod_unlink("/test_img/t_err_file");
        kmod_rmdir("/test_img/t_err_dir");
        pass(N);
    }

    // ---- fd (file descriptors) ----
    void test_fd(CapIdx root_cap) {
        const char *N = "fd";
        const char *P = "/test_img/t_fd";

        int fd = kmod_mkfile(P, "w+");
        check(N, fd >= 0, "create failed");
        kmod_fwrite(fd, "0123456789abcdef", 16);
        kmod_fclose(fd);

        int fd1 = kmod_fopen(P, "r");
        int fd2 = kmod_fopen(P, "r");
        check(N, fd1 >= 0 && fd2 >= 0, "open two fds failed");

        char b1[8] = {}, b2[8] = {};
        size_t r1 = kmod_fread(fd1, b1, 4);
        check(N, r1 == 4 && memcmp(b1, "0123", 4) == 0, "fd1 read");

        size_t r2 = kmod_fread(fd2, b2, 4);
        check(N, r2 == 4 && memcmp(b2, "0123", 4) == 0, "fd2 read independent");

        kmod_fclose(fd1);
        // read on closed fd
        check(N, kmod_fread(fd1, b1, 1) == 0, "read closed fd");

        kmod_fclose(fd2);
        // invalid fd
        check(N, kmod_fread(999, b1, 1) == 0, "read invalid fd");
        check(N, kmod_fwrite(999, "x", 1) == 0, "write invalid fd");
        kmod_fclose(999);  // should not crash

        kmod_unlink(P);
        pass(N);
    }

    // ---- dirents ----
    void test_dirents(CapIdx root_cap) {
        const char *N = "dirents";
        const int kCount = 20;

        check(N, kmod_mkdir("/test_img/t_dirents") == 0, "mkdir failed");

        for (int i = 0; i < kCount; ++i) {
            char p[64];
            snprintf(p, sizeof(p), "/test_img/t_dirents/e_%d", i);
            int fd = kmod_mkfile(p, "w+");
            check(N, fd >= 0, p);
            char v = static_cast<char>('A' + (i % 26));
            kmod_fwrite(fd, &v, 1);
            kmod_fclose(fd);
        }

        // count entries via getdents — single call with large buffer
        CapIdx dir =
            sys_vfs_opendir(root_cap, "test_img/t_dirents", flags::O_READ)
                .value();
        check(N, dir != cap::null && dir != cap::error, "opendir failed");
        int found = 0;
        char dent[65536];
        size_t bytes = sys_vfs_getdents(dir, dent, sizeof(dent), 0).value();
        printf("test_fs_score: getdents returned %u bytes\n", static_cast<unsigned>(bytes));
        for (size_t off = 0; off < bytes;) {
            auto *h = reinterpret_cast<const dir_entry_header *>(dent + off);
            const char *nm = dent + off + sizeof(dir_entry_header);
            if (nm[0] == 'e' && nm[1] == '_') ++found;
            off += h->next_offset;
        }
        (void)sys_cap_remove(dir).to_result();
        printf("test_fs_score: dirents found=%d expected=%d\n", found, kCount);
        check(N, found == kCount, "dirent count mismatch");

        for (int i = 0; i < kCount; ++i) {
            char p[64];
            snprintf(p, sizeof(p), "/test_img/t_dirents/e_%d", i);
            kmod_unlink(p);
        }
        kmod_rmdir("/test_img/t_dirents");
        pass(N);
    }

    // ---- path (nested dirs) ----
    void test_path(CapIdx root_cap) {
        const char *N = "path";

        check(N, kmod_mkdir("/test_img/t_path") == 0, "mkdir l1 failed");
        check(N, kmod_mkdir("/test_img/t_path/l2") == 0, "mkdir l2 failed");

        int fd = kmod_mkfile("/test_img/t_path/l2/deep", "w+");
        check(N, fd >= 0, "create deep file failed");
        kmod_fwrite(fd, "deep_data", 9);
        kmod_fclose(fd);

        fd = kmod_fopen("/test_img/t_path/l2/deep", "r");
        check(N, fd >= 0, "reopen deep file failed");
        char buf[32] = {};
        size_t r = kmod_fread(fd, buf, sizeof(buf));
        kmod_fclose(fd);
        check(N, r == 9 && memcmp(buf, "deep_data", 9) == 0, "deep data mismatch");

        kmod_unlink("/test_img/t_path/l2/deep");
        kmod_rmdir("/test_img/t_path/l2");
        kmod_rmdir("/test_img/t_path");
        pass(N);
    }

    // ---- big (large file) ----
    void test_big(CapIdx root_cap) {
        const char *N = "big";
        const char *P = "/test_img/t_big";
        constexpr size_t kChunks = 64;
        constexpr size_t kChunkSz = 1024;

        int fd = kmod_mkfile(P, "w+");
        check(N, fd >= 0, "create big failed");

        char buf[kChunkSz];
        for (size_t i = 0; i < kChunks; ++i) {
            for (size_t j = 0; j < kChunkSz; ++j)
                buf[j] = static_cast<char>((i * 17 + j) & 0xFF);
            size_t w = kmod_fwrite(fd, buf, kChunkSz);
            check(N, w == kChunkSz, "big write chunk failed");
        }
        kmod_fclose(fd);

        fd = kmod_fopen(P, "r");
        for (size_t i = 0; i < kChunks; ++i) {
            memset(buf, 0, kChunkSz);
            size_t r = kmod_fread(fd, buf, kChunkSz);
            check(N, r == kChunkSz, "big read chunk failed");
            for (size_t j = 0; j < kChunkSz; ++j) {
                if (static_cast<unsigned char>(buf[j]) !=
                    static_cast<unsigned char>((i * 17 + j) & 0xFF)) {
                    fail(N, "big data mismatch");
                }
            }
        }
        kmod_fclose(fd);
        kmod_unlink(P);
        pass(N);
    }
}  // namespace

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;
    printf("test_fs_score: start pid=%u\n", sys_getpid(__pcb_cap).value());

    CapIdx root_cap = bootstrap_root_dir();
    if (root_cap == cap::null || root_cap == cap::error) {
        printf("test_fs_score: FAIL bootstrap\n");
        exit(-1);
    }

    test_basic(root_cap);
    test_rw(root_cap);
    test_dir(root_cap);
    test_names(root_cap);
    test_stress(root_cap);
    test_errors(root_cap);
    test_fd(root_cap);
    test_dirents(root_cap);
    test_path(root_cap);
    test_big(root_cap);

    printf("test_fs_score: ALL PASS\n");
    exit(0);
    return 0;
}
