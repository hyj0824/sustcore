/**
 * @file syscall.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief kmod系统调用接口
 * @version alpha-1.0.0
 * @date 2026-05-14
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#include <cstddef>
#include <cstdint>
#include <sustcore/capability.h>

struct ReceiveToken {
    size_t sender_id;
    size_t record_idx;
    size_t timestamp;
};

extern "C" {
void kwrites(const char *str, size_t len);
size_t sys_grow_vma(size_t heap_base, size_t newbrk);
size_t create_process(const char *path);

bool sys_tmp_write(size_t idx, uint64_t value);
uint64_t sys_tmp_read(size_t idx);

bool sys_cap_clone(CapIdx src, CapIdx target);
bool sys_cap_downgrade(CapIdx idx, uint64_t new_perm);
bool sys_cap_derive(CapIdx src, CapIdx target, uint64_t new_perm);
bool sys_cap_send(CapIdx src, size_t target_pid, ReceiveToken *token);
bool sys_cap_recv(CapIdx target, ReceiveToken *token);

bool sys_create_notification(CapIdx target);
bool sys_signal_notification(CapIdx capidx, size_t idx);
bool sys_unsignal_notification(CapIdx capidx, size_t idx);
bool sys_check_notification(CapIdx capidx, size_t idx);
bool sys_wait_notification(CapIdx capidx, size_t idx);
}

extern "C" {
int kputs(const char *str);
size_t brk(size_t newbrk);
void *sbrk(ptrdiff_t increment);
}