/**
 * @file virtio.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief virtio 规范
 * @version alpha-1.0.0
 * @date 2026-06-07
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <sus/types.h>

namespace virtio {
    enum class Type {
        NETWORK           = 0x1001,
        BLOCK             = 0x1002,
        MEMORY_BALLOONING = 0x1002,
        CONSOLE           = 0x1003,
        SCSI_HOST         = 0x1004,
        ENTROPY           = 0x1005,
        TRANSPORT_9P      = 0x1009
    };
    using u8   = uint8_t;
    using u16  = uint16_t;
    using u32  = uint32_t;
    using u64  = uint64_t;
    using le16 = uint16_t;
    using le32 = uint32_t;
    using le64 = uint64_t;
    using be16 = uint16_t;
    using be32 = uint32_t;
    using be64 = uint64_t;

    namespace offset {
        constexpr size_t MAGIC_VALUE        = 0x000;  // r
        constexpr size_t VERSION            = 0x004;  // r
        constexpr size_t DEVICE_ID          = 0x008;  // r
        constexpr size_t VENDOR_ID          = 0x00C;  // r
        constexpr size_t DEVICE_FEATURE     = 0x010;  // r
        constexpr size_t DEVICE_FEATURE_SEL = 0x014;  // w
        constexpr size_t DRIVER_FEATURE     = 0x020;  // r
        constexpr size_t DRIVER_FEATURE_SEL = 0x024;  // w
        constexpr size_t QUEUE_SEL          = 0x030;  // w
        constexpr size_t QUEUE_NUM_MAX      = 0x034;  // r
        constexpr size_t QUEUE_NUM          = 0x038;  // w
        constexpr size_t QUEUE_READY        = 0x044;  // rw
        constexpr size_t QUEUE_NOTIFY       = 0x050;  // w
        constexpr size_t INTERRUPT_STATUS   = 0x060;  // r
        constexpr size_t INTERRUPT_ACK      = 0x064;  // w
        constexpr size_t STATUS             = 0x070;  // rw
        constexpr size_t QUEUE_DESC_LO      = 0x080;
        constexpr size_t QUEUE_DESC_HI      = 0x084;  // w
        constexpr size_t QUEUE_DRIVER_LO    = 0x090;
        constexpr size_t QUEUE_DRIVER_HI    = 0x094;  // w
        constexpr size_t QUEUE_DEVICE_LO    = 0x0A0;
        constexpr size_t QUEUE_DEVICE_HI    = 0x0A4;  // w
        constexpr size_t SHM_SEL            = 0x0AC;  // w
        constexpr size_t SHM_LEN_LO         = 0x0B0;
        constexpr size_t SHM_LEN_HI         = 0x0B4;  // r
        constexpr size_t SHM_BASE_LO        = 0x0B8;
        constexpr size_t SHM_BASE_HI        = 0x0BC;  // r
        constexpr size_t QUEUE_RESET        = 0x0C0;  // w
        constexpr size_t CONFIG_GENERATION  = 0x0FC;  // r
        constexpr size_t CONFIG_SPACE       = 0x100;  // rw
        constexpr size_t TOTAL_SIZE         = 0x100;  // 256 bytes
    }  // namespace offset

    // ------------------------------------------------------------------------
    // MMIO 通用寄存器布局（与 offset 命名空间的偏移量严格对应）
    // 基于 Virtio 1.2 规范 Table 4.1，所有寄存器 32 位宽
    // ------------------------------------------------------------------------
    struct CommonConfiguration {
        // 0x000 : Magic value (R)
        le32 magic_value;
        // 0x004 : Version (R)
        le32 version;
        // 0x008 : Device ID (R)
        le32 device_id;
        // 0x00C : Vendor ID (R)
        le32 vendor_id;
        // 0x010 : Device Features (R)
        le32 device_features;
        // 0x014 : Device Features Select (W)
        le32 device_features_sel;
        // 0x018 - 0x01C : Reserved
        le32 reserved_0x018;
        le32 reserved_0x01C;
        // 0x020 : Driver Features (W)
        le32 driver_features;
        // 0x024 : Driver Features Select (W)
        le32 driver_features_sel;
        // 0x028 - 0x02C : Reserved
        le32 reserved_0x028;
        le32 reserved_0x02C;
        // 0x030 : Queue Select (W)
        le32 queue_sel;
        // 0x034 : Queue Num Max (R)
        le32 queue_num_max;
        // 0x038 : Queue Num (W)
        le32 queue_num;
        // 0x03C - 0x040 : Reserved
        le32 reserved_0x03C;
        le32 reserved_0x040;
        // 0x044 : Queue Ready (RW)
        le32 queue_ready;
        // 0x048 - 0x04C : Reserved
        le32 reserved_0x048;
        le32 reserved_0x04C;
        // 0x050 : Queue Notify (W)
        le32 queue_notify;
        // 0x054 - 0x05C : Reserved
        le32 reserved_0x054;
        le32 reserved_0x058;
        le32 reserved_0x05C;
        // 0x060 : Interrupt Status (R)
        le32 interrupt_status;
        // 0x064 : Interrupt Acknowledge (W)
        le32 interrupt_ack;
        // 0x068 - 0x06C : Reserved
        le32 reserved_0x068;
        le32 reserved_0x06C;
        // 0x070 : Device Status (RW)
        le32 status;
        // 0x074 - 0x07C : Reserved
        le32 reserved_0x074;
        le32 reserved_0x078;
        le32 reserved_0x07C;
        // 0x080 : Queue Descriptor Low (W)
        le32 queue_desc_low;
        // 0x084 : Queue Descriptor High (W)
        le32 queue_desc_high;
        // 0x088 - 0x08C : Reserved
        le32 reserved_0x088;
        le32 reserved_0x08C;
        // 0x090 : Queue Driver Low (W)
        le32 queue_driver_low;
        // 0x094 : Queue Driver High (W)
        le32 queue_driver_high;
        // 0x098 - 0x09C : Reserved
        le32 reserved_0x098;
        le32 reserved_0x09C;
        // 0x0A0 : Queue Device Low (W)
        le32 queue_device_low;
        // 0x0A4 : Queue Device High (W)
        le32 queue_device_high;
        // 0x0A8 : Reserved (4 bytes)
        le32 reserved_0x0A8;
        // 0x0AC : Shared Memory Select (W)
        le32 shm_sel;
        // 0x0B0 : Shared Memory Length Low (R)
        le32 shm_len_low;
        // 0x0B4 : Shared Memory Length High (R)
        le32 shm_len_high;
        // 0x0B8 : Shared Memory Base Low (R)
        le32 shm_base_low;
        // 0x0BC : Shared Memory Base High (R)
        le32 shm_base_high;
        // 0x0C0 : Queue Reset (RW)
        le32 queue_reset;
        // 0x0C4 - 0x0F8 : Reserved (14 dwords)
        le32 reserved_0x0C4[14];
        // 0x0FC : Configuration Generation (R)
        le32 config_generation;
    };
    static_assert(sizeof(CommonConfiguration) == offset::TOTAL_SIZE,
                  "CommonConfiguration must be exactly 256 bytes");

    // 可选：校验关键字段偏移是否与 offset 命名空间一致
    static_assert(offsetof(CommonConfiguration, magic_value) ==
                  offset::MAGIC_VALUE);
    static_assert(offsetof(CommonConfiguration, version) == offset::VERSION);
    static_assert(offsetof(CommonConfiguration, device_id) ==
                  offset::DEVICE_ID);
    static_assert(offsetof(CommonConfiguration, vendor_id) ==
                  offset::VENDOR_ID);
    static_assert(offsetof(CommonConfiguration, device_features) ==
                  offset::DEVICE_FEATURE);
    static_assert(offsetof(CommonConfiguration, device_features_sel) ==
                  offset::DEVICE_FEATURE_SEL);
    static_assert(offsetof(CommonConfiguration, driver_features) ==
                  offset::DRIVER_FEATURE);
    static_assert(offsetof(CommonConfiguration, driver_features_sel) ==
                  offset::DRIVER_FEATURE_SEL);
    static_assert(offsetof(CommonConfiguration, queue_sel) ==
                  offset::QUEUE_SEL);
    static_assert(offsetof(CommonConfiguration, queue_num_max) ==
                  offset::QUEUE_NUM_MAX);
    static_assert(offsetof(CommonConfiguration, queue_num) ==
                  offset::QUEUE_NUM);
    static_assert(offsetof(CommonConfiguration, queue_ready) ==
                  offset::QUEUE_READY);
    static_assert(offsetof(CommonConfiguration, queue_notify) ==
                  offset::QUEUE_NOTIFY);
    static_assert(offsetof(CommonConfiguration, interrupt_status) ==
                  offset::INTERRUPT_STATUS);
    static_assert(offsetof(CommonConfiguration, interrupt_ack) ==
                  offset::INTERRUPT_ACK);
    static_assert(offsetof(CommonConfiguration, status) == offset::STATUS);
    static_assert(offsetof(CommonConfiguration, queue_desc_low) ==
                  offset::QUEUE_DESC_LO);
    static_assert(offsetof(CommonConfiguration, queue_desc_high) ==
                  offset::QUEUE_DESC_HI);
    static_assert(offsetof(CommonConfiguration, queue_driver_low) ==
                  offset::QUEUE_DRIVER_LO);
    static_assert(offsetof(CommonConfiguration, queue_driver_high) ==
                  offset::QUEUE_DRIVER_HI);
    static_assert(offsetof(CommonConfiguration, queue_device_low) ==
                  offset::QUEUE_DEVICE_LO);
    static_assert(offsetof(CommonConfiguration, queue_device_high) ==
                  offset::QUEUE_DEVICE_HI);
    static_assert(offsetof(CommonConfiguration, shm_sel) == offset::SHM_SEL);
    static_assert(offsetof(CommonConfiguration, shm_len_low) ==
                  offset::SHM_LEN_LO);
    static_assert(offsetof(CommonConfiguration, shm_len_high) ==
                  offset::SHM_LEN_HI);
    static_assert(offsetof(CommonConfiguration, shm_base_low) ==
                  offset::SHM_BASE_LO);
    static_assert(offsetof(CommonConfiguration, shm_base_high) ==
                  offset::SHM_BASE_HI);
    static_assert(offsetof(CommonConfiguration, queue_reset) ==
                  offset::QUEUE_RESET);
    static_assert(offsetof(CommonConfiguration, config_generation) ==
                  offset::CONFIG_GENERATION);

    constexpr size_t MAGIC_VALUE = 0x74726976;
    constexpr size_t VERSION = 0x2;
}  // namespace virtio