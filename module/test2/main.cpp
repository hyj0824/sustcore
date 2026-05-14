#include <cstddef>
#include <cstdio>
#include <kmod/syscall.h>

constexpr CapIdx kNotifCap = cap::make(0, 1);

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
    printf("test2: start\n");

    wait_and_clear_flag(kTmpTest1Ready);

    ReceiveToken token{};
    token.sender_id = sys_tmp_read(kTmpTokenBase + 0);
    token.record_idx = sys_tmp_read(kTmpTokenBase + 1);
    token.timestamp = sys_tmp_read(kTmpTokenBase + 2);

    if (!sys_cap_recv(kNotifCap, &token)) {
        printf("test2: cap_recv failed\n");
        while (true) {}
    }

    sys_tmp_write(kTmpTest2Ready, 1);

    sys_wait_notification(kNotifCap, kSignalSyn);
    printf("test2: SYN-ACK\n");
    sys_unsignal_notification(kNotifCap, kSignalSyn);
    sys_signal_notification(kNotifCap, kSignalAck);

    while (true) {}
    return 0;
}
