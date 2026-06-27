#include <kmod/syscall.h>

#include <cstdio>
#include <cstring>

extern "C" const unsigned char elf_demand_perf_blob[];
extern "C" const unsigned char elf_demand_perf_blob_end[];

namespace {
    constexpr size_t PAGE_SIZE = 4096;
    volatile uint64_t g_sink   = 0;

    bool should_touch(int argc, const char *argv[]) {
        return argc >= 2 && argv != nullptr && argv[1] != nullptr &&
               strcmp(argv[1], "touch") == 0;
    }
}  // namespace

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
                         const bsheader *bsargv[]) {
    (void)envp;
    (void)bsargv;

    if (should_touch(argc, argv)) {
        auto *blob = reinterpret_cast<const volatile unsigned char *>(
            elf_demand_perf_blob);
        size_t size = static_cast<size_t>(elf_demand_perf_blob_end -
                                          elf_demand_perf_blob);
        for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
            g_sink += blob[offset];
        }
    }
    exit(0);
    return 0;
}
