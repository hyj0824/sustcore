/**
 * @file entry.cpp
 * @author OpenAI
 * @brief linux subsystem libc entry dispatcher
 * @version alpha-1.0.0
 * @date 2026-06-22
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <prm.h>

extern "C" size_t linuxss_entry(const void *stack_sp, size_t init_a0,
                                size_t init_a1, size_t init_a2) {
    static_cast<void>(init_a0);
    static_cast<void>(init_a1);
    static_cast<void>(init_a2);

    auto *words = static_cast<const uint64_t *>(stack_sp);
    size_t argc = words[0];
    auto *argv  = reinterpret_cast<const char *const *>(words + 1);

    size_t offset = 1 + argc + 1;
    auto *envp    = reinterpret_cast<const char *const *>(words + offset);
    while (words[offset] != 0) {
        ++offset;
    }
    ++offset;

    auto *auxv = reinterpret_cast<const Elf64_auxv_t *>(words + offset);
    while (true) {
        auto *entry =
            reinterpret_cast<const Elf64_auxv_t *>(words + offset);
        offset += 2;
        if (entry->a_type == AT_NULL) {
            break;
        }
    }

    size_t bsargc = words[offset];
    ++offset;
    auto *bsargv =
        reinterpret_cast<const bsheader *const *>(words + offset);
    linuxss_restore_runtime_from_bootstrap(
        bsargc, const_cast<const bsheader **>(bsargv));
    linux_main(stack_sp, argc, const_cast<const char **>(argv),
               const_cast<const char **>(envp), auxv, bsargc,
               const_cast<const bsheader **>(bsargv));
    return 0;
}
