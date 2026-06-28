/**
 * @file main.cpp
 * @author theflysong
 * @brief /proc/meminfo 基础测试
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
    constexpr const char *MEMINFO_PATH = "/proc/meminfo";

    void fail(const char *msg) {
        printf("test_meminfo: FAIL %s\n", msg);
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

    [[nodiscard]]
    bool contains_text(const char *buf, size_t len, const char *needle) {
        if (buf == nullptr || needle == nullptr) {
            return false;
        }
        size_t needle_len = strlen(needle);
        if (needle_len == 0 || needle_len > len) {
            return false;
        }
        for (size_t i = 0; i + needle_len <= len; ++i) {
            if (memcmp(buf + i, needle, needle_len) == 0) {
                return true;
            }
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

    int fd = kmod_fopen(MEMINFO_PATH, "r");
    check(fd >= 0, "open /proc/meminfo failed");

    CapIdx cap = kmod_getcap(fd);
    check(cap != cap::null && cap != cap::error, "meminfo cap invalid");

    char buf[4096]{};
    size_t total = read_all(cap, buf, sizeof(buf) - 1);
    check(total > 0, "empty /proc/meminfo");
    buf[total] = '\0';
    printf("test_meminfo: /proc/meminfo\n===========================\n%s===========================\n", buf);

    check(contains_text(buf, total, "MemTotal:"), "missing MemTotal");
    check(contains_text(buf, total, "MemFree:"), "missing MemFree");
    check(contains_text(buf, total, "MemAvailable:"), "missing MemAvailable");
    check(contains_text(buf, total, "Buffers:"), "missing Buffers");
    check(contains_text(buf, total, "Cached:"), "missing Cached");
    check(contains_text(buf, total, "KernelStack:"), "missing KernelStack");
    check(contains_text(buf, total, "PageTables:"), "missing PageTables");
    check(contains_text(buf, total, "Hugepagesize:"), "missing Hugepagesize");

    kmod_fclose(fd);
    printf("test_meminfo: PASS\n");
    exit(0);
    return 0;
}
