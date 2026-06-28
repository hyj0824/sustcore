/**
 * @file busybox.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief busybox 测试运行逻辑头文件
 * @version alpha-1.0.0
 * @date 2026-06-28
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

namespace contest_runner {
    constexpr const char *BUSYBOX_COMMAND_LINES[] = {
        R"(echo "#### independent command test")",
        "ash -c exit",
        "sh -c exit",
        "basename /aaa/bbb",
        "cal",
        "clear",
        "date",
        "df",
        "dirname /aaa/bbb",
        "dmesg",
        // "du",                        (会造成卡死)
        "expr 1 + 1",
        "false",
        "true",
        "which ls",
        "uname",
        "uptime",
        R"(printf "abc\n")",
        "ps",
        "pwd",
        "free",
        "hwclock",
        // "sh -c 'sleep 5' & ./busybox kill $!"
        "ls",
        "sleep 1",
        R"(echo "#### file opration test")",
        "touch test.txt",
        "cat test.txt",
        "cut -c 3 test.txt",
        "od test.txt",
        "head test.txt",
        "tail test.txt",
        "hexdump -C test.txt",
        "md5sum test.txt",
        "stat test.txt",
        "strings test.txt",
        "wc test.txt",
        "more test.txt",
        "rm test.txt",
        "mkdir test_dir",
        "mv test_dir test",
        "rmdir test",
        "grep hello busybox_cmd.txt",
        "cp busybox_cmd.txt busybox_cmd.bak",
        "rm busybox_cmd.bak",
        // R"(find -name "busybox_cmd.txt")",
        nullptr,
    };
}  // namespace contest_runner
