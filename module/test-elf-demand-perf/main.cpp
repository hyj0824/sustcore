#include <kmod/syscall.h>

#include <cstdio>

namespace {
    constexpr const char *CHILD_PATH = "/initrd/test-elf-demand-perf-child.mod";
    constexpr size_t RUNS            = 3;

    void fail(const char *msg) {
        printf("test_elf_demand_perf: FAIL %s\n", msg);
        exit(-1);
    }

    void check(bool condition, const char *msg) {
        if (!condition) {
            fail(msg);
        }
    }

    VFSPageCacheStats stats(bool reset = false) {
        VFSPageCacheStats out{};
        check(sys_vfs_page_cache_stats(0, &out, reset),
              "page cache stats syscall failed");
        return out;
    }

    struct RunMetrics {
        uint64_t create_ns;
        uint64_t total_ns;
        VFSPageCacheStats after_create;
        VFSPageCacheStats after_wait;
    };

    CapIdx spawn_child(CapIdx image_cap, const char *mode) {
        const char *argv[] = {"test-elf-demand-perf-child", mode, nullptr};
        ExecveRequest request{
            .image_cap = image_cap,
            .execfn    = CHILD_PATH,
            .caps      = nullptr,
            .argv      = argv,
            .envp      = nullptr,
            .bsargv    = nullptr,
        };
        auto create_res = sys_create_process(SCHED_CLASS_FCFS, &request)
                              .to_result();
        return create_res.has_value() ? create_res.value() : cap::error;
    }

    RunMetrics run_once(CapIdx image_cap, const char *mode) {
        (void)stats(true);
        uint64_t start_ns   = sys_time_now_ns().value();
        CapIdx child        = spawn_child(image_cap, mode);
        uint64_t created_ns = sys_time_now_ns().value();
        check(child != cap::null && child != cap::error,
              "create child process failed");

        VFSPageCacheStats after_create = stats();
        CapIdx wait_caps[]             = {child, cap::null};
        auto wait_res =
            sys_tcb_wait(__main_tcb_cap, wait_caps, nullptr, 0).to_result();
        uint64_t done_ns = sys_time_now_ns().value();
        check(wait_res.has_value(), "wait child process failed");

        VFSPageCacheStats after_wait = stats();
        return RunMetrics{
            .create_ns    = created_ns - start_ns,
            .total_ns     = done_ns - start_ns,
            .after_create = after_create,
            .after_wait   = after_wait,
        };
    }

    void print_metrics(const char *mode, size_t run,
                       const RunMetrics &metrics) {
        printf(
            "test_elf_demand_perf: mode=%s run=%lu create_ns=%lu total_ns=%lu "
            "create_reads=%u total_reads=%u create_misses=%u total_misses=%u\n",
            mode, static_cast<unsigned long>(run),
            static_cast<unsigned long>(metrics.create_ns),
            static_cast<unsigned long>(metrics.total_ns),
            static_cast<unsigned>(metrics.after_create.backing_reads),
            static_cast<unsigned>(metrics.after_wait.backing_reads),
            static_cast<unsigned>(metrics.after_create.misses),
            static_cast<unsigned>(metrics.after_wait.misses));
    }

    uint64_t run_mode(CapIdx image_cap, const char *mode,
                      uint64_t &total_runtime_ns) {
        uint64_t create_sum = 0;
        total_runtime_ns    = 0;
        for (size_t run = 0; run < RUNS; ++run) {
            RunMetrics metrics = run_once(image_cap, mode);
            print_metrics(mode, run, metrics);
            create_sum       += metrics.create_ns;
            total_runtime_ns += metrics.total_ns;
        }
        return create_sum;
    }
}  // namespace

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
                         const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;

    printf("test_elf_demand_perf: start pid=%u child=%s runs=%lu\n",
           sys_getpid(__pcb_cap).value(), CHILD_PATH,
           static_cast<unsigned long>(RUNS));

    int fd = kmod_fopen(CHILD_PATH, "x");
    check(fd >= 0, "open child ELF failed");
    CapIdx image_cap = kmod_getcap(fd);
    check(image_cap != cap::null && image_cap != cap::error,
          "child image capability missing");

    uint64_t no_touch_total  = 0;
    uint64_t touch_total     = 0;
    uint64_t no_touch_create = run_mode(image_cap, "no-touch", no_touch_total);
    uint64_t touch_create    = run_mode(image_cap, "touch", touch_total);

    printf(
        "test_elf_demand_perf: avg_create_no_touch_ns=%lu "
        "avg_create_touch_ns=%lu avg_total_no_touch_ns=%lu "
        "avg_total_touch_ns=%lu\n",
        static_cast<unsigned long>(no_touch_create / RUNS),
        static_cast<unsigned long>(touch_create / RUNS),
        static_cast<unsigned long>(no_touch_total / RUNS),
        static_cast<unsigned long>(touch_total / RUNS));
    printf("test_elf_demand_perf: PASS\n");

    kmod_fclose(fd);
    exit(0);
    return 0;
}
