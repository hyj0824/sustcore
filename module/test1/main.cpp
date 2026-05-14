#include <cstddef>
#include <cstdio>
#include <kmod/syscall.h>

constexpr CapIdx kNotifCap = cap::make(0, 1);
constexpr CapIdx kSendCap  = cap::make(0, 2);

constexpr size_t kSignalSyn = 0;
constexpr size_t kSignalAck = 1;

constexpr size_t kTmpTokenBase  = 0;
constexpr size_t kTmpTest2Ready = 6;
constexpr size_t kTmpTest1Ready = 7;

static void wait_and_clear_flag(size_t idx) {
    while (true) {
        if (sys_tmp_read(idx) != 0) {
            sys_tmp_write(idx, 0);
            return;
        }
    }
}

int kmod_main() {
    printf("test1: start\n");

    if (!sys_create_notification(kNotifCap)) {
        printf("test1: create notification failed\n");
        while (true) {}
    }

    if (!sys_cap_clone(kNotifCap, kSendCap)) {
        printf("test1: clone notification failed\n");
        while (true) {}
    }

    size_t test2_pid = create_process("/initrd/test2.mod");
    if (test2_pid == 0) {
        printf("test1: create test2 failed\n");
        while (true) {}
    }

    ReceiveToken token{};
    if (!sys_cap_send(kSendCap, test2_pid, &token)) {
        printf("test1: cap_send failed\n");
        while (true) {}
    }

    sys_tmp_write(kTmpTokenBase + 0, token.sender_id);
    sys_tmp_write(kTmpTokenBase + 1, token.record_idx);
    sys_tmp_write(kTmpTokenBase + 2, token.timestamp);
    sys_tmp_write(kTmpTest1Ready, 1);

    wait_and_clear_flag(kTmpTest2Ready);

    printf("test1: SYN\n");
    sys_signal_notification(kNotifCap, kSignalSyn);

    sys_wait_notification(kNotifCap, kSignalAck);
    printf("test1: ACK\n");

    while (true) {}
    return 0;
}
