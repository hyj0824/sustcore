/**
 * @file main.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 文件系统测试 B
 * @version alpha-1.0.0
 * @date 2026-06-11
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include <kmod/syscall.h>

#include <cstdio>
#include <cstring>

namespace {
    constexpr const char *TMPFS_FILE = "/tmpfs/abc/file1.txt";
    constexpr size_t BUFFER_SIZE     = 512;
}

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
              const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;
    printf("test_file_rw_b: start pid=%u\n", sys_getpid(__pcb_cap).value());

    int fd = kmod_fopen(TMPFS_FILE, "r");
    if (fd < 0) {
        printf("test_file_rw_b: open failed: %s\n", TMPFS_FILE);
        exit(-1);
    }

    char content[BUFFER_SIZE] = {};
    size_t total              = 0;
    while (total + 1 < BUFFER_SIZE) {
        size_t got = kmod_fread(fd, content + total, BUFFER_SIZE - total - 1);
        if (got == 0) {
            break;
        }
        total += got;
    }
    kmod_fclose(fd);
    content[total] = '\0';

    if (total == 0) {
        printf("test_file_rw_b: read empty content\n");
        exit(-1);
    }

    printf("test_file_rw_b: file content begin\n%s\n", content);
    printf("test_file_rw_b: file content end\n");
    printf("test_file_rw_b: done\n");
    exit(0);
    return 0;
}
