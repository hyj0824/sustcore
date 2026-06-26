/**
 * @file ltp.cpp
 * @author theflysong
 * @brief ltp 测试运行逻辑
 * @version alpha-1.0.0
 * @date 2026-06-25
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "runner.h"

#include <sustcore/files.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "ltp.h"

namespace contest_runner {
    namespace {
        constexpr size_t PATH_BUFFER_SIZE   = 256;

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
    }  // namespace

    TestRunStats run_ltp(const RunnerContext &ctx) {
        TestRunStats stats{};
        printf("#### OS COMP TEST GROUP START ltp-%s ####\n", ctx.libc_name);

        char ltp_root[PATH_BUFFER_SIZE]{};
        if (!make_path(ltp_root, sizeof(ltp_root), ctx.libc_root,
                       "ltp/testcases/bin"))
        {
            printf("contest-runner: invalid ltp root %s\n", ctx.libc_root);
            printf("#### OS COMP TEST GROUP END ltp-%s ####\n", ctx.libc_name);
            return stats;
        }

        OpenDirHandle cwd{};
        if (!open_cwd_dir(ltp_root, cwd)) {
            printf("#### OS COMP TEST GROUP END ltp-%s ####\n", ctx.libc_name);
            return stats;
        }

        for (size_t group_idx = 0; ltp::testcases[group_idx] != nullptr;
             ++group_idx)
        {
            auto testcase_group = ltp::testcases[group_idx];
            for (size_t case_idx = 0; testcase_group[case_idx] != nullptr;
                 ++case_idx)
            {
                const char *case_name = testcase_group[case_idx];
                ++stats.total;
                printf("RUN LTP CASE %s\n", case_name);

                char case_path[PATH_BUFFER_SIZE]{};
                if (!make_path(case_path, sizeof(case_path), ltp_root,
                               case_name))
                {
                    ++stats.failed;
                    printf("FAIL LTP CASE %s : -1\n", case_name);
                    continue;
                }

                const char *argv[] = {case_name, nullptr};
                int status         = 0;
                auto err = run_program(ctx, cwd, case_path, argv, status);
                if (err != RunProgramError::NONE) {
                    ++stats.failed;
                    printf("FAIL LTP CASE %s : -1\n", case_name);
                    printf("contest-runner: ltp failed error=%s case=%s\n",
                           run_error_string(err), case_name);
                    continue;
                }

                int ret = run_exit_code(status);
                printf("FAIL LTP CASE %s : %d\n", case_name, ret);
                if (run_status_success(status)) {
                    ++stats.passed;
                } else {
                    ++stats.failed;
                }
            }
        }

        close_cwd_dir(cwd);
        printf("#### OS COMP TEST GROUP END ltp-%s ####\n", ctx.libc_name);
        return stats;
    }
}  // namespace contest_runner
