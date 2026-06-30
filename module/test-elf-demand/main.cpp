/**
 * @file main.cpp
 * @brief ELF demand paging verification — file-backed memory, short-read zeros, COW fork
 */

#include <kmod/syscall.h>

#include <cstdio>
#include <cstring>

namespace {
    constexpr const char *TEST_FILE_1 = "/test_img/elf_demand_test_1";
    constexpr const char *TEST_FILE_2 = "/test_img/elf_demand_test_2";
    constexpr const char *TEST_FILE_3 = "/test_img/elf_demand_test_3";
    constexpr size_t PAGE_SIZE          = 4096;
    constexpr size_t FILE_SIZE          = PAGE_SIZE * 2;
    constexpr uintptr_t MAP_ADDR_1      = 0x000700000000ULL;
    constexpr uintptr_t MAP_ADDR_2      = 0x000700002000ULL;
    constexpr uintptr_t MAP_ADDR_3      = 0x000700004000ULL;
    constexpr uint64_t MEMORY_GROWTH_FIXED = 0;
    constexpr uint64_t PROT_READ        = 0x1;
    constexpr uint64_t PROT_WRITE       = 0x2;
    constexpr uint64_t PROT_RW          = PROT_READ | PROT_WRITE;

    char g_data[FILE_SIZE];

    void fail(const char *msg) {
        printf("test_elf_demand: FAIL %s\n", msg);
        exit(-1);
    }

    void check(bool condition, const char *msg) {
        if (!condition) {
            fail(msg);
        }
    }

    void fill_data() {
        for (size_t i = 0; i < FILE_SIZE; ++i) {
            size_t page = i / PAGE_SIZE;
            g_data[i] = static_cast<char>('a' + ((page * 7 + i) % 26));
        }
    }
}  // namespace

extern "C" int kmod_main(int argc, const char *argv[], const char *envp[],
                         const bsheader *bsargv[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    (void)bsargv;

    printf("test_elf_demand: start pid=%u\n", sys_getpid(__pcb_cap).value());
    fill_data();

    // ── Scenario 1: file-backed memory reading ──────────────────────────
    printf("test_elf_demand: scenario 1 — file-backed read\n");
    {
        kmod_unlink(TEST_FILE_1);
        int fd = kmod_mkfile(TEST_FILE_1, "w+");
        check(fd >= 0, "scenario 1: create test file failed");

        CapIdx file_cap = kmod_getcap(fd);
        check(file_cap != cap::null && file_cap != cap::error,
              "scenario 1: file capability missing");

        size_t wrote = sys_vfs_write(file_cap, 0, g_data, sizeof(g_data)).value();
        check(wrote == sizeof(g_data), "scenario 1: write test file failed");
        check(sys_vfs_sync(file_cap), "scenario 1: sync test file failed");

        // Map second page (offset = PAGE_SIZE)
        auto mem_res = sys_mem_create(file_cap, PAGE_SIZE, false, false,
                                      MEMORY_GROWTH_FIXED, PAGE_SIZE)
                           .to_result();
        CapIdx mem_cap = mem_res.has_value() ? mem_res.value() : cap::error;
        check(mem_cap != cap::null && mem_cap != cap::error,
              "scenario 1: file-backed memory create failed");

        kmod_fclose(fd);

        auto *mapped = reinterpret_cast<const char *>(MAP_ADDR_1);
        check(sys_pcb_map(__pcb_cap, mem_cap, 0, const_cast<char *>(mapped),
                          PAGE_SIZE, PROT_READ),
              "scenario 1: map file-backed memory failed");

        check(std::memcmp(mapped, g_data + PAGE_SIZE, PAGE_SIZE) == 0,
              "scenario 1: mapped page does not match file content");

        check(sys_mem_unmap(mem_cap, const_cast<char *>(mapped)),
              "scenario 1: unmap file-backed memory failed");
        check(sys_cap_remove(mem_cap),
              "scenario 1: remove memory cap failed");
        kmod_unlink(TEST_FILE_1);

        printf("test_elf_demand: scenario 1 PASS\n");
    }

    // ── Scenario 2: short file → BSS-like zero fill ─────────────────────
    printf("test_elf_demand: scenario 2 — short-read zeros\n");
    {
        kmod_unlink(TEST_FILE_2);
        int fd = kmod_mkfile(TEST_FILE_2, "w+");
        check(fd >= 0, "scenario 2: create test file failed");

        CapIdx file_cap = kmod_getcap(fd);
        check(file_cap != cap::null && file_cap != cap::error,
              "scenario 2: file capability missing");

        // Write only one page of data — file is smaller than the mapped region
        size_t wrote = sys_vfs_write(file_cap, 0, g_data, PAGE_SIZE).value();
        check(wrote == PAGE_SIZE, "scenario 2: write test file failed");
        check(sys_vfs_sync(file_cap), "scenario 2: sync test file failed");

        // Create MemoryPayload with memsz=2 pages, but file only has 1 page
        auto mem_res = sys_mem_create(file_cap, FILE_SIZE, false, false,
                                      MEMORY_GROWTH_FIXED, 0)
                           .to_result();
        CapIdx mem_cap = mem_res.has_value() ? mem_res.value() : cap::error;
        check(mem_cap != cap::null && mem_cap != cap::error,
              "scenario 2: file-backed memory create failed");

        kmod_fclose(fd);

        auto *mapped = reinterpret_cast<const char *>(MAP_ADDR_2);
        check(sys_pcb_map(__pcb_cap, mem_cap, 0, const_cast<char *>(mapped),
                          FILE_SIZE, PROT_READ),
              "scenario 2: map file-backed memory failed");

        // First page: should match written data
        check(std::memcmp(mapped, g_data, PAGE_SIZE) == 0,
              "scenario 2: first page does not match file content");

        // Second page: beyond file end — must be all zeros (BSS behavior)
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            check(mapped[PAGE_SIZE + i] == 0,
                  "scenario 2: byte beyond file end is not zero");
        }

        check(sys_mem_unmap(mem_cap, const_cast<char *>(mapped)),
              "scenario 2: unmap file-backed memory failed");
        check(sys_cap_remove(mem_cap),
              "scenario 2: remove memory cap failed");
        kmod_unlink(TEST_FILE_2);

        printf("test_elf_demand: scenario 2 PASS\n");
    }

    // ── Scenario 3: COW fork on file-backed memory ──────────────────────
    printf("test_elf_demand: scenario 3 — COW fork\n");
    {
        kmod_unlink(TEST_FILE_3);
        int fd = kmod_mkfile(TEST_FILE_3, "w+");
        check(fd >= 0, "scenario 3: create test file failed");

        CapIdx file_cap = kmod_getcap(fd);
        check(file_cap != cap::null && file_cap != cap::error,
              "scenario 3: file capability missing");

        size_t wrote = sys_vfs_write(file_cap, 0, g_data, PAGE_SIZE).value();
        check(wrote == PAGE_SIZE, "scenario 3: write test file failed");
        check(sys_vfs_sync(file_cap), "scenario 3: sync test file failed");

        auto mem_res = sys_mem_create(file_cap, PAGE_SIZE, false, false,
                                      MEMORY_GROWTH_FIXED, 0)
                           .to_result();
        CapIdx mem_cap = mem_res.has_value() ? mem_res.value() : cap::error;
        check(mem_cap != cap::null && mem_cap != cap::error,
              "scenario 3: file-backed memory create failed");

        kmod_fclose(fd);

        auto *mapped = reinterpret_cast<char *>(MAP_ADDR_3);
        check(sys_pcb_map(__pcb_cap, mem_cap, 0, mapped,
                          PAGE_SIZE, PROT_RW),
              "scenario 3: map file-backed memory failed");

        // Verify initial content before fork
        check(std::memcmp(mapped, g_data, PAGE_SIZE) == 0,
              "scenario 3: initial content mismatch");

        CapIdx child_pcb_cap = cap::null;
        ForkCaps fork_caps{
            .child_pcb_cap      = child_pcb_cap,
            .child_main_tcb_cap = cap::null,
        };
        auto fork_res = fork(&fork_caps).to_result();
        child_pcb_cap = fork_caps.child_pcb_cap;
        check(fork_res.has_value() && child_pcb_cap != cap::error,
              "scenario 3: fork failed");

        size_t child_pid = fork_res.value();
        bool is_child    = (child_pid == 0);
        (void)is_child;

        if (is_child) {
            // Child: overwrite mapped data with new pattern
            for (size_t i = 0; i < PAGE_SIZE; ++i) {
                mapped[i] = static_cast<char>('Z' + (i % 26));
            }
            // Verify child sees its own write
            for (size_t i = 0; i < PAGE_SIZE; ++i) {
                check(mapped[i] == static_cast<char>('Z' + (i % 26)),
                      "scenario 3: child readback mismatch after COW write");
            }
            printf("test_elf_demand: scenario 3 child write OK\n");

            check(sys_mem_unmap(mem_cap, mapped),
                  "scenario 3: child unmap failed");
            exit(0);
        }

        // Parent: verify original data is unchanged after child's COW write
        check(std::memcmp(mapped, g_data, PAGE_SIZE) == 0,
              "scenario 3: parent data corrupted after child COW write");
        printf("test_elf_demand: scenario 3 parent data intact\n");

        check(sys_mem_unmap(mem_cap, mapped),
              "scenario 3: parent unmap failed");
        check(sys_cap_remove(mem_cap),
              "scenario 3: remove memory cap failed");
        kmod_unlink(TEST_FILE_3);

        printf("test_elf_demand: scenario 3 PASS\n");
    }

    printf("test_elf_demand: PASS\n");
    exit(0);
    return 0;
}
