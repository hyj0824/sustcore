/**
 * @file main.cpp
 * @author Codex
 * @brief Ext4 create-file tests
 * @version alpha-1.0.0
 * @date 2026-06-16
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <kmod/bootstrap.h>
#include <kmod/syscall.h>

#include <cstdio>
#include <cstring>

namespace {
    constexpr const char *CREATE_PATH = "/test_img/codex_created_file";
    constexpr const char *CREATE_NAME = "codex_created_file";
    char g_dirent_buffer[16384];
    char g_entry_name_buffer[256];

    [[nodiscard]]
    CapIdx bootstrap_root_dir() {
        CapIdx cap = cap::null;
        bool found = false;
        bool ok    = bootstrap_foreach_record(
            __startup_data, __startup_size,
            [&](const BootstrapRecordView &view) {
                if (found || view.header->type != BOOTSTRAP_TYPE_DIRCAPEXPLAIN)
                {
                    return;
                }
                BootstrapCapPathView cap_path {};
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

    [[nodiscard]]
    bool dir_has_entry(CapIdx dir_cap, const char *entry_name,
                       bool expect_file) {
        if (entry_name == nullptr) {
            return false;
        }
        const size_t entry_name_len = strlen(entry_name);
        if (entry_name_len >= sizeof(g_entry_name_buffer)) {
            return false;
        }
        memcpy(g_entry_name_buffer, entry_name, entry_name_len + 1);
        size_t doff = 0;
        while (doff != DIR_ENTRY_END) {
            CapInfo cap_info {};
            if (!sys_cap_lookup(dir_cap, &cap_info) ||
                cap_info.type != PayloadType::VDIR)
            {
                printf("test_ext4_create: getdents cap invalid cap=%u type=%u doff=%u\n",
                       static_cast<unsigned>(dir_cap),
                       static_cast<unsigned>(cap_info.type),
                       static_cast<unsigned>(doff));
                return false;
            }
            memset(g_dirent_buffer, 0, sizeof(g_dirent_buffer));
            const size_t bytes =
                sys_vfs_getdents(dir_cap, g_dirent_buffer,
                                 sizeof(g_dirent_buffer), doff);
            if (bytes == 0) {
                return false;
            }

            size_t parsed = 0;
            for (size_t offset = 0; offset < bytes;) {
                if (bytes - offset < sizeof(dir_entry_header)) {
                    return false;
                }
                auto *header =
                    reinterpret_cast<const dir_entry_header *>(
                        g_dirent_buffer + offset);
                const char *name =
                    g_dirent_buffer + offset + sizeof(dir_entry_header);
                const size_t name_room =
                    bytes - offset - sizeof(dir_entry_header);
                if (memchr(name, '\0', name_room) == nullptr) {
                    return false;
                }

                const size_t name_len = strlen(name);
                if (name_len == entry_name_len &&
                    memcmp(name, g_entry_name_buffer, entry_name_len) == 0 &&
                    header->is_file == expect_file)
                {
                    return true;
                }
                ++parsed;

                if (header->next_offset == DIR_ENTRY_END) {
                    return false;
                }
                if (header->next_offset == 0 ||
                    offset + header->next_offset > bytes)
                {
                    return false;
                }
                offset += header->next_offset;
            }
            if (parsed == 0) {
                return false;
            }
            doff += parsed;
        }
        return false;
    }
}  // namespace

int kmod_main() {
    printf("test_ext4_create: start pid=%u\n", sys_getpid(__pcb_cap));

    CapIdx root_cap = bootstrap_root_dir();
    if (root_cap == cap::null || root_cap == cap::error) {
        printf("test_ext4_create: bootstrap root dir missing\n");
        exit(-1);
    }

    int fd = kmod_mkfile(CREATE_PATH, "w+");
    if (fd < 0) {
        fd = kmod_fopen(CREATE_PATH, "r");
        if (fd < 0) {
            printf("test_ext4_create: create/open %s failed\n", CREATE_PATH);
            exit(-1);
        }
        printf("test_ext4_create: %s already exists\n", CREATE_PATH);
    } else {
        printf("test_ext4_create: created %s\n", CREATE_PATH);
    }

    const size_t size = sys_vfs_size(kmod_getcap(fd));
    kmod_fclose(fd);
    if (size != 0) {
        printf("test_ext4_create: created file size mismatch=%u\n",
               static_cast<unsigned>(size));
        exit(-1);
    }

    CapIdx ext4_dir = sys_vfs_opendir(root_cap, "test_img", flags::O_READ);
    if (ext4_dir == cap::null || ext4_dir == cap::error) {
        printf("test_ext4_create: opendir /test_img failed\n");
        exit(-1);
    }
    const bool found = dir_has_entry(ext4_dir, CREATE_NAME, true);
    sys_cap_remove(ext4_dir);
    if (!found) {
        printf("test_ext4_create: created file not found in root dir\n");
        exit(-1);
    }

    printf("test_ext4_create: PASS\n");
    exit(0);
    return 0;
}
