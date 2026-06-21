/**
 * @file main.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief POSIX subsystem main file
 * @version alpha-1.0.0
 * @date 2026-06-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <elf.h>
#include <std/stdio.h>
#include <sus/types.h>
#include <sustcore/bootstrap.h>
#include <sustcore/syscall_str.h>
#include <prm.h>
#include <syscall.h>

#include <cstddef>
#include <cstring>
#include <syscall.h.in>

extern "C" bool g_linux_initialized = false;

extern "C" size_t linux_dispatch(size_t a0, size_t a1, size_t a2, size_t a3,
                                 size_t a4, size_t a5, size_t a6, size_t a7);

#define INVALID_VALUE 0xFFFF'FFFF'FFFF'FFFF

namespace {
    constexpr size_t UTSNAME_FIELD_SIZE = 65;

    struct linux_utsname {
        char sysname[UTSNAME_FIELD_SIZE];
        char nodename[UTSNAME_FIELD_SIZE];
        char release[UTSNAME_FIELD_SIZE];
        char version[UTSNAME_FIELD_SIZE];
        char machine[UTSNAME_FIELD_SIZE];
        char domainname[UTSNAME_FIELD_SIZE];
    };

    struct linux_iovec {
        const void *iov_base;
        size_t iov_len;
    };

    void copy_uts_field(char (&dst)[UTSNAME_FIELD_SIZE],
                        const char *src) noexcept {
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, UTSNAME_FIELD_SIZE - 1);
        dst[UTSNAME_FIELD_SIZE - 1] = '\0';
    }

    const char *linux_machine_name() noexcept {
#if defined(__ARCH_riscv64__)
        return "riscv64";
#elif defined(__ARCH_loongarch64__)
        return "loongarch64";
#else
        return "unknown";
#endif
    }
}  // namespace

const char *at_to_string(int a_type) {
    switch (a_type) {
        case AT_NULL:              return "AT_NULL";
        case AT_IGNORE:            return "AT_IGNORE";
        case AT_EXECFD:            return "AT_EXECFD";
        case AT_PHDR:              return "AT_PHDR";
        case AT_PHENT:             return "AT_PHENT";
        case AT_PHNUM:             return "AT_PHNUM";
        case AT_PAGESZ:            return "AT_PAGESZ";
        case AT_BASE:              return "AT_BASE";
        case AT_FLAGS:             return "AT_FLAGS";
        case AT_ENTRY:             return "AT_ENTRY";
        case AT_NOTELF:            return "AT_NOTELF";
        case AT_UID:               return "AT_UID";
        case AT_EUID:              return "AT_EUID";
        case AT_GID:               return "AT_GID";
        case AT_EGID:              return "AT_EGID";
        case AT_PLATFORM:          return "AT_PLATFORM";
        case AT_HWCAP:             return "AT_HWCAP";
        case AT_CLKTCK:            return "AT_CLKTCK";
        case AT_FPUCW:             return "AT_FPUCW";
        case AT_DCACHEBSIZE:       return "AT_DCACHEBSIZE";
        case AT_ICACHEBSIZE:       return "AT_ICACHEBSIZE";
        case AT_UCACHEBSIZE:       return "AT_UCACHEBSIZE";
        case AT_IGNOREPPC:         return "AT_IGNOREPPC";
        case AT_SECURE:            return "AT_SECURE";
        case AT_BASE_PLATFORM:     return "AT_BASE_PLATFORM";
        case AT_RANDOM:            return "AT_RANDOM";
        case AT_HWCAP2:            return "AT_HWCAP2";
        case AT_RSEQ_FEATURE_SIZE: return "AT_RSEQ_FEATURE_SIZE";
        case AT_RSEQ_ALIGN:        return "AT_RSEQ_ALIGN";
        case AT_HWCAP3:            return "AT_HWCAP3";
        case AT_HWCAP4:            return "AT_HWCAP4";
        case AT_EXECFN:            return "AT_EXECFN";
        case AT_SYSINFO:           return "AT_SYSINFO";
        case AT_SYSINFO_EHDR:      return "AT_SYSINFO_EHDR";
        case AT_L1I_CACHESHAPE:    return "AT_L1I_CACHESHAPE";
        case AT_L1D_CACHESHAPE:    return "AT_L1D_CACHESHAPE";
        case AT_L2_CACHESHAPE:     return "AT_L2_CACHESHAPE";
        case AT_L3_CACHESHAPE:     return "AT_L3_CACHESHAPE";
        case AT_L1I_CACHESIZE:     return "AT_L1I_CACHESIZE";
        case AT_L1I_CACHEGEOMETRY: return "AT_L1I_CACHEGEOMETRY";
        case AT_L1D_CACHESIZE:     return "AT_L1D_CACHESIZE";
        case AT_L1D_CACHEGEOMETRY: return "AT_L1D_CACHEGEOMETRY";
        case AT_L2_CACHESIZE:      return "AT_L2_CACHESIZE";
        case AT_L2_CACHEGEOMETRY:  return "AT_L2_CACHEGEOMETRY";
        case AT_L3_CACHESIZE:      return "AT_L3_CACHESIZE";
        case AT_L3_CACHEGEOMETRY:  return "AT_L3_CACHEGEOMETRY";
        case AT_MINSIGSTKSZ:       return "AT_MINSIGSTKSZ";
        default:                   return "UNKNOWN";
    }
}

void dump_vector(const char *name, const char *const *items) {
    if (items == nullptr) {
        printf("%s: (null)\n", name);
        return;
    }
    for (size_t i = 0; items[i] != nullptr; ++i) {
        printf("%s[%u] = %s\n", name, static_cast<unsigned>(i), items[i]);
    }
}

void dump_auxv(const Elf64_auxv_t *auxv) {
    if (auxv == nullptr) {
        printf("auxv: (null)\n");
        return;
    }
    for (size_t i = 0; auxv[i].a_type != AT_NULL; ++i) {
        printf("auxv[%u] = { type=%s, val=%p }\n", static_cast<unsigned>(i),
               at_to_string(auxv[i].a_type),
               reinterpret_cast<void *>(auxv[i].a_un.a_val));
    }
    printf("auxv[end] = { type=%s, val=%p }\n", at_to_string(AT_NULL), nullptr);
}

void dump_bsargv(size_t bsargc, const bsheader *const *bsargv) {
    if (bsargv == nullptr) {
        printf("bsargv: (null)\n");
        return;
    }
    for (size_t i = 0; i < bsargc; ++i) {
        const bsheader *record = bsargv[i];
        if (record == nullptr) {
            printf("bsargv[%u] = nullptr\n", static_cast<unsigned>(i));
            continue;
        }
        BootstrapRecordView view{};
        if (!bootstrap_make_view(record, view)) {
            printf("bsargv[%u] = { type=%u, size=%u, invalid=true }\n",
                   static_cast<unsigned>(i), record->type, record->size);
            continue;
        }

        switch (record->type) {
            case boot::TYPE_CAPEXP: {
                BootstrapCapExplainView cap_view{};
                if (!bootstrap_parse_cap_explain(view, cap_view)) {
                    printf("bsargv[%u] = { type=TYPE_CAPEXP, size=%u, "
                           "parse_error=true }\n",
                           static_cast<unsigned>(i), record->size);
                    continue;
                }
                printf(
                    "bsargv[%u] = { type=TYPE_CAPEXP, size=%u, cap_idx=%lu, "
                    "cap_type=%s, cap_perm=0x%016lx, cap_desc=%s }\n",
                    static_cast<unsigned>(i), record->size, cap_view.cap_idx,
                    to_string(cap_view.cap_type), cap_view.cap_perm,
                    cap_view.cap_desc);
                continue;
            }
            case boot::TYPE_VADDREXP: {
                BootstrapVaddrExplainView vaddr_view{};
                if (!bootstrap_parse_vaddr_explain(view, vaddr_view)) {
                    printf("bsargv[%u] = { type=TYPE_VADDREXP, size=%u, "
                           "parse_error=true }\n",
                           static_cast<unsigned>(i), record->size);
                    continue;
                }
                printf(
                    "bsargv[%u] = { type=TYPE_VADDREXP, size=%u, vaddr=%p, "
                    "vaddr_desc=%s }\n",
                    static_cast<unsigned>(i), record->size,
                    vaddr_view.vaddr.addr(), vaddr_view.vaddr_desc);
                continue;
            }
            default:
                printf("bsargv[%u] = { type=%u, size=%u, raw_data=%p, "
                       "raw_size=%u }\n",
                       static_cast<unsigned>(i), record->type, record->size,
                       view.data, static_cast<unsigned>(view.data_size));
                continue;
        }
    }
}

void stack_dump(const void *stack_sp, const Elf64_auxv_t *auxv) {
    if (auxv == nullptr) {
        printf("stack_dump: auxv=null\n");
        return;
    }
    if (stack_sp == nullptr) {
        printf("stack_dump: stack_sp=null\n");
        return;
    }

    auto *begin = static_cast<const uint64_t *>(stack_sp);
    auto *end   = reinterpret_cast<const Elf64_auxv_t *>(auxv);
    while (end->a_type != AT_NULL) {
        ++end;
    }
    ++end;

    auto *word_end = reinterpret_cast<const uint64_t *>(end);
    for (auto *line = begin; line < word_end; line += 2) {
        printf("%p:", line);
        for (size_t i = 0; i < 2 && line + i < word_end; ++i) {
            printf(" 0x%016lx", line[i]);
        }
        printf("\n");
    }
}

extern "C" void linux_main(const void *stack_sp, size_t argc,
                           const char *argv[], const char *envp[],
                           const Elf64_auxv_t *auxv, size_t bsargc,
                           const bsheader *bsargv[]) {
    g_linux_initialized = true;
    // puts("linux-subsystem: initialized");
    // printf("stack dump:\n");
    // stack_dump(stack_sp, auxv);
    // printf("argc & argv:\n");
    // printf("argc = %u\n", static_cast<unsigned>(argc));
    // dump_vector("argv", argv);
    // printf("\nenvp:\n");
    // dump_vector("envp", envp);
    // printf("\nauxv:\n");
    // dump_auxv(auxv);
    printf("\nbsargc & bsargv:\n");
    printf("bsargc = %u\n", static_cast<unsigned>(bsargc));
    dump_bsargv(bsargc, bsargv);
}

size_t linux_sys_write(size_t fd, const void *buf, size_t len) {
    if (fd == 1 || fd == 2) {
        sys_write_serial(reinterpret_cast<const char *>(buf), len);
        return len;
    }
    printf("linux-subsystem: unsupported fd %d\n", fd);
    return INVALID_VALUE;
}

size_t linux_sys_writev(size_t fd, const linux_iovec *iov, size_t iovcnt) {
    if (fd != 1 && fd != 2) {
        printf("linux-subsystem: unsupported fd %d\n", fd);
        return INVALID_VALUE;
    }
    if (iov == nullptr && iovcnt != 0) {
        return INVALID_VALUE;
    }

    size_t total = 0;
    for (size_t i = 0; i < iovcnt; ++i) {
        if (iov[i].iov_base == nullptr && iov[i].iov_len != 0) {
            return INVALID_VALUE;
        }
        sys_write_serial(reinterpret_cast<const char *>(iov[i].iov_base),
                         iov[i].iov_len);
        total += iov[i].iov_len;
    }
    return total;
}

size_t linux_sys_brk(size_t newbrk) {
    return linuxss_brk(newbrk);
}

size_t linux_sys_uname(void *buf) {
    if (buf == nullptr) {
        return INVALID_VALUE;
    }

    linux_utsname uts{};
    copy_uts_field(uts.sysname, "sustcore");
    copy_uts_field(uts.nodename, "qemu");
    copy_uts_field(uts.release, "4.15.0");
    copy_uts_field(uts.version, "build 0");
    copy_uts_field(uts.machine, linux_machine_name());
    copy_uts_field(uts.domainname, "(none)");
    memcpy(buf, &uts, sizeof(uts));
    return 0;
}

extern "C" size_t linux_dispatch(size_t a0, size_t a1, size_t a2, size_t a3,
                                 size_t a4, size_t a5, size_t a6, size_t a7) {
    switch (a7) {
        case __NR_write:
            return linux_sys_write(a0, reinterpret_cast<const void *>(a1), a2);
        case __NR_writev:
            return linux_sys_writev(
                a0, reinterpret_cast<const linux_iovec *>(a1), a2);
        case __NR_brk: return linux_sys_brk(a0);
        case __NR_uname:
            return linux_sys_uname(reinterpret_cast<void *>(a0));
        case __NR_faccessat:
            return -2;
        default:
            printf("linux-subsystem: unsupported syscall %s (%d)\n",
                   syscall_to_string(a7), a7);
            printf("linux-subsystem: syscall arguments: a0=%p, a1=%p, a2=%p, a3=%p, "
                   "a4=%p, a5=%p, a6=%p\n",
                   reinterpret_cast<void *>(a0), reinterpret_cast<const char *>(a1),
                   reinterpret_cast<void *>(a2), reinterpret_cast<void *>(a3),
                   reinterpret_cast<void *>(a4), reinterpret_cast<void *>(a5),
                   reinterpret_cast<void *>(a6));
            // 先卡死以进行测试
            while (true);
            return INVALID_VALUE;
    }
}
