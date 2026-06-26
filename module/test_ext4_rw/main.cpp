/**
 * @file main.cpp
 * @brief Ext4 read-write and mkdir tests
 */

#include <sustcore/bootstrap.h>
#include <kmod/syscall.h>

#include <cstdio>
#include <cstring>

namespace {
    constexpr const char *RW_FILE     = "/test_img/rw_test_file";
    constexpr const char *RW_NAME     = "rw_test_file";
    constexpr const char *MKDIR_PATH  = "/test_img/rw_test_dir";
    constexpr const char *MKDIR_NAME  = "rw_test_dir";
    constexpr const char *TEST_STR    = "hello ext4 write!";
    constexpr size_t TEST_STR_LEN     = 18;
    char g_read_buf[256];
    char g_dirent_buf[16384];
    char g_entry_name_buf[256];

    [[nodiscard]]
    CapIdx bootstrap_root_dir() {
        CapIdx cap = cap::null;
        bool found = false;
        bool ok    = bootstrap_foreach_record(
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
                cap   = cap_explain.cap_idx;
                found = true;
            });
        return ok && found ? cap : cap::null;
    }

    [[nodiscard]]
    bool dir_has_entry(CapIdx dir_cap, const char *entry_name, bool expect_file) {
        if (entry_name == nullptr) return false;
        const size_t entry_name_len = strlen(entry_name);
        if (entry_name_len >= sizeof(g_entry_name_buf)) return false;
        memcpy(g_entry_name_buf, entry_name, entry_name_len + 1);
        size_t doff = 0;
        while (doff != DIR_ENTRY_END) {
            CapInfo cap_info {};
            if (!sys_cap_lookup(dir_cap, &cap_info) ||
                cap_info.type != PayloadType::VDIR) return false;
            memset(g_dirent_buf, 0, sizeof(g_dirent_buf));
            auto getdents_res =
                sys_vfs_getdents(dir_cap, g_dirent_buf, sizeof(g_dirent_buf), doff)
                    .to_result();
            if (!getdents_res.has_value()) return false;
            const size_t bytes = getdents_res.value();
            if (bytes == 0) return false;
            size_t parsed = 0;
            for (size_t offset = 0; offset < bytes;) {
                if (bytes - offset < sizeof(dir_entry_header)) return false;
                auto *header = reinterpret_cast<const dir_entry_header *>(
                    g_dirent_buf + offset);
                const char *name = g_dirent_buf + offset + sizeof(dir_entry_header);
                const size_t name_room = bytes - offset - sizeof(dir_entry_header);
                if (memchr(name, '\0', name_room) == nullptr) return false;
                const size_t name_len = strlen(name);
                if (name_len == entry_name_len &&
                    memcmp(name, g_entry_name_buf, entry_name_len) == 0) {
                    NodeMeta st {};
                    if (!sys_vfs_stat(dir_cap, name, &st))
                        return false;
                    return expect_file ? st.type == EntryType::FILE
                                       : st.type == EntryType::DIR;
                }
                ++parsed;
                if (header->next_offset == DIR_ENTRY_END) break;
                if (header->next_offset == 0 || offset + header->next_offset > bytes)
                    break;
                offset += header->next_offset;
            }
            if (parsed == 0) return false;
            doff += parsed;
        }
        return false;
    }
}  // namespace

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;
    printf("test_ext4_rw: start pid=%u\n", sys_getpid(__pcb_cap).value());

    CapIdx root_cap = bootstrap_root_dir();
    if (root_cap == cap::null || root_cap == cap::error) {
        printf("test_ext4_rw: bootstrap root dir missing\n");
        exit(-1);
    }

    // --- write test ---
    printf("test_ext4_rw: creating %s\n", RW_FILE);
    int fd = kmod_mkfile(RW_FILE, "w+");
    int ret;
    if (fd < 0) {
        fd = kmod_fopen(RW_FILE, "r+");
        if (fd < 0) {
            printf("test_ext4_rw: create/open %s failed\n", RW_FILE);
            exit(-1);
        }
    }

    size_t written = kmod_fwrite(fd, TEST_STR, TEST_STR_LEN);
    if (written != TEST_STR_LEN) {
        printf("test_ext4_rw: write failed wrote=%u expected=%u\n",
               static_cast<unsigned>(written),
               static_cast<unsigned>(TEST_STR_LEN));
        kmod_fclose(fd);
        exit(-1);
    }
    printf("test_ext4_rw: wrote %u bytes\n", static_cast<unsigned>(written));

    const size_t file_size = sys_vfs_size(kmod_getcap(fd)).value();
    if (file_size != TEST_STR_LEN) {
        printf("test_ext4_rw: size mismatch after write got=%u expected=%u\n",
               static_cast<unsigned>(file_size),
               static_cast<unsigned>(TEST_STR_LEN));
        kmod_fclose(fd);
        exit(-1);
    }

    kmod_fclose(fd);

    // reopen for reading (offset back to 0)
    fd = kmod_fopen(RW_FILE, "r");
    if (fd < 0) {
        printf("test_ext4_rw: reopen for read failed\n");
        exit(-1);
    }
    memset(g_read_buf, 0, sizeof(g_read_buf));
    size_t got = kmod_fread(fd, g_read_buf, TEST_STR_LEN);
    if (got != TEST_STR_LEN) {
        printf("test_ext4_rw: read failed got=%u expected=%u\n",
               static_cast<unsigned>(got),
               static_cast<unsigned>(TEST_STR_LEN));
        kmod_fclose(fd);
        exit(-1);
    }
    kmod_fclose(fd);

    if (memcmp(g_read_buf, TEST_STR, TEST_STR_LEN) != 0) {
        printf("test_ext4_rw: data mismatch '%.18s' vs '%.18s'\n",
               g_read_buf, TEST_STR);
        exit(-1);
    }
    printf("test_ext4_rw: write+read PASS\n");

    // --- large write test (exercises extent split) ---
    printf("test_ext4_rw: large write test\n");
    kmod_fclose(fd);
    fd = kmod_mkfile("/test_img/large_file", "w+");
    if (fd < 0) {
        fd = kmod_fopen("/test_img/large_file", "r+");
        if (fd < 0) {
            printf("test_ext4_rw: create large_file failed\n");
            exit(-1);
        }
    }
    constexpr size_t kLargeSize = 10240;
    char large_buf[kLargeSize];
    for (size_t i = 0; i < kLargeSize; ++i) large_buf[i] = static_cast<char>(i & 0xFF);
    written = kmod_fwrite(fd, large_buf, kLargeSize);
    if (written != kLargeSize) {
        printf("test_ext4_rw: large write failed wrote=%u\n",
               static_cast<unsigned>(written));
        kmod_fclose(fd);
        exit(-1);
    }
    kmod_fclose(fd);
    fd = kmod_fopen("/test_img/large_file", "r");
    if (fd < 0) { printf("test_ext4_rw: reopen large_file failed\n"); exit(-1); }
    memset(large_buf, 0, kLargeSize);
    got = kmod_fread(fd, large_buf, kLargeSize);
    kmod_fclose(fd);
    if (got != kLargeSize) {
        printf("test_ext4_rw: large read failed got=%u\n",
               static_cast<unsigned>(got));
        exit(-1);
    }
    for (size_t i = 0; i < kLargeSize; ++i) {
        if (static_cast<unsigned char>(large_buf[i]) != static_cast<unsigned char>(i & 0xFF)) {
            printf("test_ext4_rw: large data mismatch at %u\n",
                   static_cast<unsigned>(i));
            exit(-1);
        }
    }
    kmod_fclose(fd);
    printf("test_ext4_rw: large write+read PASS\n");

    // --- extent stress test (50 sparse 1KB writes → exercises path-based split) ---
    printf("test_ext4_rw: extent stress test\n");
    {
        fd = kmod_mkfile("/test_img/stress_file", "w+");
        if (fd < 0) { printf("test_ext4_rw: create stress_file failed\n"); exit(-1); }
        constexpr size_t kStressChunks = 50;
        constexpr size_t kChunkSz      = 1024;
        char sbuf[kChunkSz];
        for (size_t c = 0; c < kStressChunks; ++c) {
            for (size_t j = 0; j < kChunkSz; ++j)
                sbuf[j] = static_cast<char>((c * kChunkSz + j) & 0xFF);
            written = kmod_fwrite(fd, sbuf, kChunkSz);
            if (written != kChunkSz) {
                printf("test_ext4_rw: stress write chunk %u failed\n",
                       static_cast<unsigned>(c));
                kmod_fclose(fd); exit(-1);
            }
            // interleave with temp file to prevent extent merging
            int tf = kmod_mkfile("/test_img/__tmp", "w+");
            if (tf >= 0) { kmod_fwrite(tf, "X", 1); kmod_fclose(tf); }
            kmod_unlink("/test_img/__tmp");
        }
        kmod_fclose(fd);

        fd = kmod_fopen("/test_img/stress_file", "r");
        if (fd < 0) { printf("test_ext4_rw: reopen stress_file failed\n"); exit(-1); }
        for (size_t c = 0; c < kStressChunks; ++c) {
            memset(sbuf, 0, kChunkSz);
            got = kmod_fread(fd, sbuf, kChunkSz);
            if (got != kChunkSz) {
                printf("test_ext4_rw: stress read chunk %u failed\n",
                       static_cast<unsigned>(c));
                kmod_fclose(fd); exit(-1);
            }
            for (size_t j = 0; j < got; ++j) {
                if (static_cast<unsigned char>(sbuf[j]) !=
                    static_cast<unsigned char>((c * kChunkSz + j) & 0xFF)) {
                    printf("test_ext4_rw: stress data mismatch at c=%u j=%u\n",
                           static_cast<unsigned>(c), static_cast<unsigned>(j));
                    kmod_fclose(fd); exit(-1);
                }
            }
        }
        kmod_fclose(fd);
        kmod_unlink("/test_img/stress_file");
    }
    printf("test_ext4_rw: extent stress PASS\n");

    // verify file visible in directory
    auto ext4_dir_res = sys_vfs_opendir(root_cap, "test_img", flags::O_READ).to_result();
    CapIdx ext4_dir =
        ext4_dir_res.has_value() ? ext4_dir_res.value() : cap::error;
    if (ext4_dir == cap::null || ext4_dir == cap::error) {
        printf("test_ext4_rw: opendir test_img failed\n");
        exit(-1);
    }
    if (!dir_has_entry(ext4_dir, RW_NAME, true)) {
        printf("test_ext4_rw: created file not found in dir\n");
        (void)sys_cap_remove(ext4_dir).to_result();
        exit(-1);
    }
    (void)sys_cap_remove(ext4_dir).to_result();

    // --- mkdir test ---
    printf("test_ext4_rw: mkdir %s\n", MKDIR_PATH);
    int md = kmod_mkdir(MKDIR_PATH);
    if (md < 0) {
        printf("test_ext4_rw: mkdir failed\n");
        exit(-1);
    }

    ext4_dir_res = sys_vfs_opendir(root_cap, "test_img", flags::O_READ).to_result();
    ext4_dir = ext4_dir_res.has_value() ? ext4_dir_res.value() : cap::error;
    if (ext4_dir == cap::null || ext4_dir == cap::error) {
        printf("test_ext4_rw: opendir for mkdir check failed\n");
        exit(-1);
    }
    if (!dir_has_entry(ext4_dir, MKDIR_NAME, false)) {
        printf("test_ext4_rw: created dir not found\n");
        (void)sys_cap_remove(ext4_dir).to_result();
        exit(-1);
    }
    (void)sys_cap_remove(ext4_dir).to_result();

    // verify subdir is openable (read_directory filters "." / "..")
    auto sub_dir_res =
        sys_vfs_opendir(root_cap, "test_img/rw_test_dir", flags::O_READ)
            .to_result();
    CapIdx sub_dir =
        sub_dir_res.has_value() ? sub_dir_res.value() : cap::error;
    if (sub_dir == cap::null || sub_dir == cap::error) {
        printf("test_ext4_rw: opendir subdir failed\n");
        exit(-1);
    }
    (void)sys_cap_remove(sub_dir).to_result();
    printf("test_ext4_rw: mkdir PASS\n");

    // cross-directory rename (before rmdir, reuses existing dir)
    printf("test_ext4_rw: cross-rename test\n");
    fd = kmod_mkfile("/test_img/xf", "w+");
    if (fd < 0) { printf("test_ext4_rw: create xf failed\n"); exit(-1); }
    kmod_fwrite(fd, "cross_data", 10);
    kmod_fclose(fd);
    ret = kmod_rename("/test_img/xf", "/test_img/rw_test_dir/xf");
    if (ret < 0) { printf("test_ext4_rw: cross rename failed\n"); exit(-1); }
    ext4_dir_res = sys_vfs_opendir(root_cap, "test_img", flags::O_READ).to_result();
    ext4_dir = ext4_dir_res.has_value() ? ext4_dir_res.value() : cap::error;
    if (ext4_dir == cap::null || ext4_dir == cap::error) {
        printf("test_ext4_rw: opendir for cross-rename check failed\n"); exit(-1);
    }
    if (dir_has_entry(ext4_dir, "xf", true)) {
        printf("test_ext4_rw: old name still visible after cross-rename\n");
        (void)sys_cap_remove(ext4_dir).to_result(); exit(-1);
    }
    (void)sys_cap_remove(ext4_dir).to_result();
    fd = kmod_fopen("/test_img/rw_test_dir/xf", "r");
    if (fd < 0) { printf("test_ext4_rw: new name not openable after cross-rename\n"); exit(-1); }
    memset(g_read_buf, 0, sizeof(g_read_buf));
    got = kmod_fread(fd, g_read_buf, 20);
    kmod_fclose(fd);
    if (got != 10 || memcmp(g_read_buf, "cross_data", 10) != 0) {
        printf("test_ext4_rw: cross-rename content mismatch\n"); exit(-1);
    }
    kmod_unlink("/test_img/rw_test_dir/xf");
    printf("test_ext4_rw: cross-rename PASS\n");

    // --- unlink test ---
    printf("test_ext4_rw: unlink %s\n", RW_FILE);
    ret = kmod_unlink(RW_FILE);
    if (ret < 0) {
        printf("test_ext4_rw: unlink failed\n");
        exit(-1);
    }

    // verify file no longer visible
    ext4_dir_res = sys_vfs_opendir(root_cap, "test_img", flags::O_READ).to_result();
    ext4_dir = ext4_dir_res.has_value() ? ext4_dir_res.value() : cap::error;
    if (ext4_dir == cap::null || ext4_dir == cap::error) {
        printf("test_ext4_rw: opendir for unlink check failed\n");
        exit(-1);
    }
    if (dir_has_entry(ext4_dir, RW_NAME, true)) {
        printf("test_ext4_rw: unlinked file still visible!\n");
        (void)sys_cap_remove(ext4_dir).to_result();
        exit(-1);
    }
    (void)sys_cap_remove(ext4_dir).to_result();
    printf("test_ext4_rw: unlink PASS\n");

    // --- rmdir test ---
    printf("test_ext4_rw: rmdir %s\n", MKDIR_PATH);
    ret = kmod_rmdir(MKDIR_PATH);
    if (ret < 0) {
        printf("test_ext4_rw: rmdir failed\n");
        exit(-1);
    }
    ext4_dir_res = sys_vfs_opendir(root_cap, "test_img", flags::O_READ).to_result();
    ext4_dir = ext4_dir_res.has_value() ? ext4_dir_res.value() : cap::error;
    if (ext4_dir == cap::null || ext4_dir == cap::error) {
        printf("test_ext4_rw: opendir for rmdir check failed\n");
        exit(-1);
    }
    if (dir_has_entry(ext4_dir, MKDIR_NAME, false)) {
        printf("test_ext4_rw: removed dir still visible!\n");
        (void)sys_cap_remove(ext4_dir).to_result();
        exit(-1);
    }
    (void)sys_cap_remove(ext4_dir).to_result();
    printf("test_ext4_rw: rmdir PASS\n");

    // --- rename test ---
    printf("test_ext4_rw: rename test\n");
    fd = kmod_mkfile("/test_img/rename_src", "w+");
    if (fd < 0) { printf("test_ext4_rw: create rename_src failed\n"); exit(-1); }
    kmod_fwrite(fd, "rename_data", 11);
    kmod_fclose(fd);

    ret = kmod_rename("/test_img/rename_src", "/test_img/rename_dst");
    if (ret < 0) { printf("test_ext4_rw: rename failed\n"); exit(-1); }

    fd = kmod_fopen("/test_img/rename_dst", "r");
    if (fd < 0) {
        printf("test_ext4_rw: new name not openable after rename\n"); exit(-1);
    }
    kmod_fclose(fd);
    kmod_unlink("/test_img/rename_dst");
    printf("test_ext4_rw: rename PASS\n");

    // hard link test
    printf("test_ext4_rw: link test\n");
    fd = kmod_mkfile("/test_img/link_orig", "w+");
    if (fd < 0) { printf("test_ext4_rw: create link_orig failed\n"); exit(-1); }
    kmod_fwrite(fd, "link_data", 9);
    kmod_fclose(fd);
    ret = kmod_link("/test_img/link_new", "/test_img/link_orig");
    if (ret < 0) { printf("test_ext4_rw: link failed\n"); exit(-1); }
    // read via new link
    fd = kmod_fopen("/test_img/link_new", "r");
    if (fd < 0) { printf("test_ext4_rw: open link_new failed\n"); exit(-1); }
    memset(g_read_buf, 0, sizeof(g_read_buf));
    got = kmod_fread(fd, g_read_buf, 20);
    kmod_fclose(fd);
    if (got != 9 || memcmp(g_read_buf, "link_data", 9) != 0) {
        printf("test_ext4_rw: link content mismatch\n"); exit(-1);
    }
    // unlink original — link_new should still work (link_count=2 → 1)
    kmod_unlink("/test_img/link_orig");
    fd = kmod_fopen("/test_img/link_new", "r");
    if (fd < 0) { printf("test_ext4_rw: link_new gone after unlink orig\n"); exit(-1); }
    kmod_fclose(fd);
    kmod_unlink("/test_img/link_new");
    printf("test_ext4_rw: link PASS\n");

    // --- truncate test ---
    printf("test_ext4_rw: truncate test\n");
    fd = kmod_mkfile("/test_img/trunc_file", "w+");
    if (fd < 0) { printf("test_ext4_rw: create trunc_file failed\n"); exit(-1); }
    kmod_fwrite(fd, "1234567890", 10);
    kmod_fclose(fd);
    ret = kmod_truncate("/test_img/trunc_file", 5);
    if (ret < 0) { printf("test_ext4_rw: truncate shrink failed\n"); exit(-1); }
    fd = kmod_fopen("/test_img/trunc_file", "r");
    if (fd < 0) { printf("test_ext4_rw: reopen trunc_file failed\n"); exit(-1); }
    char tbuf[16];
    memset(tbuf, 0, sizeof(tbuf));
    got = kmod_fread(fd, tbuf, 10);
    kmod_fclose(fd);
    if (got != 5 || memcmp(tbuf, "12345", 5) != 0) {
        printf("test_ext4_rw: truncate data mismatch\n"); exit(-1);
    }
    ret = kmod_truncate("/test_img/trunc_file", 8);
    if (ret < 0) { printf("test_ext4_rw: truncate grow failed\n"); exit(-1); }
    fd = kmod_fopen("/test_img/trunc_file", "r");
    memset(tbuf, 0, sizeof(tbuf));
    got = kmod_fread(fd, tbuf, 8);
    kmod_fclose(fd);
    if (got != 8 || memcmp(tbuf, "12345\0\0\0", 8) != 0) {
        printf("test_ext4_rw: truncate grow data mismatch\n"); exit(-1);
    }
    kmod_unlink("/test_img/trunc_file");
    printf("test_ext4_rw: truncate PASS\n");

    printf("test_ext4_rw: ALL PASS\n");
    exit(0);
    return 0;
}
