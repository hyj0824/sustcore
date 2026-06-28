/**
 * @file busybox.cpp
 * @author theflysong
 * @brief busybox 测试运行逻辑
 * @version alpha-1.0.0
 * @date 2026-06-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cstdio>
#include <cstring>

#include "busybox.h"
#include "runner.h"

namespace contest_runner {
    namespace {
        constexpr size_t PATH_BUFFER_SIZE = 256;
        constexpr size_t MAX_ARGC         = 16;
        constexpr size_t MAX_ARG_BYTES    = 512;

        [[nodiscard]]
        bool make_path(char *buf, size_t bufsiz, const char *lhs,
                       const char *rhs) {
            if (buf == nullptr || bufsiz == 0 || lhs == nullptr ||
                rhs == nullptr)
            {
                return false;
            }
            int len = snprintf(buf, bufsiz, "%s/%s", lhs, rhs);
            return len > 0 && static_cast<size_t>(len) < bufsiz;
        }

        [[nodiscard]]
        bool is_space(char ch) noexcept {
            return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
        }

        [[nodiscard]]
        char decode_escape(char ch) noexcept {
            switch (ch) {
                case 'n':  return '\n';
                case '"':  return '"';
                case '\\': return '\\';
                default:   return ch;
            }
        }

        [[nodiscard]]
        bool parse_busybox_command_line(const char *line,
                                        const char *argv_out[MAX_ARGC],
                                        char storage[MAX_ARG_BYTES]) {
            if (line == nullptr || argv_out == nullptr || storage == nullptr) {
                return false;
            }

            for (size_t i = 0; i < MAX_ARGC; ++i) {
                argv_out[i] = nullptr;
            }

            size_t argc       = 0;
            size_t storage_at = 0;
            argv_out[argc++]  = "busybox";

            const char *cur = line;
            while (*cur != '\0') {
                while (is_space(*cur)) {
                    ++cur;
                }
                if (*cur == '\0') {
                    break;
                }
                if (argc + 1 >= MAX_ARGC) {
                    return false;
                }
                if (storage_at >= MAX_ARG_BYTES) {
                    return false;
                }

                argv_out[argc++] = &storage[storage_at];
                bool in_quotes    = false;
                while (*cur != '\0') {
                    if (!in_quotes && is_space(*cur)) {
                        break;
                    }
                    if (*cur == '"') {
                        in_quotes = !in_quotes;
                        ++cur;
                        continue;
                    }
                    if (*cur == '\\' && cur[1] != '\0') {
                        if (storage_at + 1 >= MAX_ARG_BYTES) {
                            return false;
                        }
                        storage[storage_at++] = decode_escape(cur[1]);
                        cur += 2;
                        continue;
                    }
                    if (storage_at + 1 >= MAX_ARG_BYTES) {
                        return false;
                    }
                    storage[storage_at++] = *cur++;
                }
                if (in_quotes) {
                    return false;
                }
                if (storage_at >= MAX_ARG_BYTES) {
                    return false;
                }
                storage[storage_at++] = '\0';
            }

            argv_out[argc] = nullptr;
            return argc > 1;
        }

        [[nodiscard]]
        int run_busybox_command_line(const RunnerContext &ctx,
                                     const OpenDirHandle &cwd,
                                     const char *busybox_path,
                                     const char *line, bool dryrun) {
            const char *argv[MAX_ARGC]{};
            char storage[MAX_ARG_BYTES]{};
            if (!parse_busybox_command_line(line, argv, storage)) {
                return -1;
            }

            if (dryrun) {
                printf("program=%s, args=", busybox_path);
                bool first = true;
                for (size_t i = 1; argv[i] != nullptr; ++i) {
                    if (!first) {
                        printf(" ");
                    }
                    printf("%s", argv[i]);
                    first = false;
                }
                printf(", stdout=\n");
                return 0;
            }

            int status = 0;
            auto err   = run_program(ctx, cwd, busybox_path, argv, status);
            if (err != RunProgramError::NONE) {
                return -1;
            }
            return run_exit_code(status);
        }
    }  // namespace

    TestRunStats run_busybox(const RunnerContext &ctx, bool dryrun) {
        TestRunStats stats{};
        printf("#### OS COMP TEST GROUP START busybox-%s ####\n",
               ctx.libc_name);

        OpenDirHandle cwd{};
        if (!open_cwd_dir(ctx.libc_root, cwd)) {
            printf("#### OS COMP TEST GROUP END busybox-%s ####\n",
                   ctx.libc_name);
            return stats;
        }

        char busybox_path[PATH_BUFFER_SIZE]{};
        if (!make_path(busybox_path, sizeof(busybox_path), ctx.libc_root,
                       "busybox"))
        {
            printf("contest-runner: invalid busybox root %s\n", ctx.libc_root);
            close_cwd_dir(cwd);
            printf("#### OS COMP TEST GROUP END busybox-%s ####\n",
                   ctx.libc_name);
            return stats;
        }

        for (size_t i = 0; BUSYBOX_COMMAND_LINES[i] != nullptr; ++i) {
            const char *line = BUSYBOX_COMMAND_LINES[i];
            ++stats.total;
            printf("contest-runner: busybox run %s\n", line);

            int ret = run_busybox_command_line(ctx, cwd, busybox_path, line,
                                               dryrun);
            if (ret != 0 && strcmp(line, "false") != 0) {
                ++stats.failed;
                printf("testcase busybox %s fail\n", line);
                continue;
            }

            ++stats.passed;
            printf("testcase busybox %s success\n", line);
        }

        close_cwd_dir(cwd);
        printf("#### OS COMP TEST GROUP END busybox-%s ####\n", ctx.libc_name);
        return stats;
    }
}  // namespace contest_runner
