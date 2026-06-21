/**
 * @file setup.cpp
 * @author OpenAI
 * @brief linux subsystem libc runtime bootstrap restore
 * @version alpha-1.0.0
 * @date 2026-06-22
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <prm.h>

#include <cstring>

size_t __linuxss_heap_base     = 0;
size_t __linuxss_ssheap_base   = 0;
size_t __linuxss_brk           = 0;
size_t __linuxss_ss_brk        = 0;
CapIdx __linuxss_pcb_cap       = cap::null;
CapIdx __linuxss_main_tcb_cap  = cap::null;
CapIdx __linuxss_heap_mem_cap  = cap::null;
CapIdx __linuxss_ssheap_mem_cap = cap::null;
size_t __linuxss_bsargc        = 0;
const bsheader **__linuxss_bsargv = nullptr;

namespace {
    bool parse_tagged_index(const char *text, const char *prefix,
                            size_t &value) {
        if (text == nullptr || prefix == nullptr) {
            return false;
        }
        size_t prefix_len = strlen(prefix);
        if (strncmp(text, prefix, prefix_len) != 0) {
            return false;
        }
        value = 0;
        for (const char *p = text + prefix_len; *p != '\0'; ++p) {
            if (*p < '0' || *p > '9') {
                return false;
            }
            value = value * 10 + static_cast<size_t>(*p - '0');
        }
        return true;
    }

    bool has_memory_kind(const char *desc, const char *kind) {
        if (desc == nullptr || kind == nullptr || desc[0] != '#') {
            return false;
        }
        ++desc;
        size_t kind_len = strlen(kind);
        return strncmp(desc, kind, kind_len) == 0 && desc[kind_len] == ':';
    }
}  // namespace

extern "C" void linuxss_restore_runtime_from_bootstrap(
    size_t bsargc, const bsheader *bsargv[]) {
    __linuxss_bsargc = bsargc;
    __linuxss_bsargv = bsargv;

    for (size_t i = 0; i < __linuxss_bsargc; ++i) {
        BootstrapRecordView view{};
        if (!bootstrap_make_view(__linuxss_bsargv[i], view)) {
            continue;
        }

        if (view.header->type == boot::TYPE_CAPEXP) {
            BootstrapCapExplainView cap_view{};
            if (!bootstrap_parse_cap_explain(view, cap_view)) {
                continue;
            }

            size_t tagged_idx = 0;
            if (cap_view.cap_type == PayloadType::PCB &&
                parse_tagged_index(cap_view.cap_desc, "#self:", tagged_idx))
            {
                __linuxss_pcb_cap = cap_view.cap_idx;
                continue;
            }
            if (cap_view.cap_type == PayloadType::TCB &&
                parse_tagged_index(cap_view.cap_desc, "#main:", tagged_idx))
            {
                __linuxss_main_tcb_cap = cap_view.cap_idx;
                continue;
            }
            if (cap_view.cap_type == PayloadType::MEMORY &&
                has_memory_kind(cap_view.cap_desc, "ss-heap"))
            {
                __linuxss_ssheap_mem_cap = cap_view.cap_idx;
                continue;
            }
            if (cap_view.cap_type == PayloadType::MEMORY &&
                has_memory_kind(cap_view.cap_desc, "heap"))
            {
                __linuxss_heap_mem_cap = cap_view.cap_idx;
                continue;
            }
        }

        if (view.header->type == boot::TYPE_VADDREXP) {
            BootstrapVaddrExplainView vaddr_view{};
            if (!bootstrap_parse_vaddr_explain(view, vaddr_view)) {
                continue;
            }
            if (strcmp(vaddr_view.vaddr_desc, "#ss-heap") == 0) {
                __linuxss_ssheap_base = vaddr_view.vaddr.arith();
                __linuxss_ss_brk      = vaddr_view.vaddr.arith();
                continue;
            }
            if (strcmp(vaddr_view.vaddr_desc, "#heap") == 0) {
                __linuxss_heap_base = vaddr_view.vaddr.arith();
                __linuxss_brk       = vaddr_view.vaddr.arith();
            }
        }
    }
}
