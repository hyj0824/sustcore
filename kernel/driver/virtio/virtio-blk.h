/**
 * @file virtio-blk.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief virtio 块设备驱动
 * @version alpha-1.0.0
 * @date 2026-06-10
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <driver/virtio/virtio.h>

namespace virtio {
    /**
     * @brief virtio 块设备 feature 位定义.
     */
    namespace feature {
        constexpr u64 MASK_BARRIER         = 1ull << 0;
        constexpr u64 MASK_SIZE_MAX        = 1ull << 1;
        constexpr u64 MASK_SEG_MAX         = 1ull << 2;
        constexpr u64 MASK_GEOMETRY        = 1ull << 4;
        constexpr u64 MASK_RO              = 1ull << 5;
        constexpr u64 MASK_BLK_SIZE        = 1ull << 6;
        constexpr u64 MASK_SCSI            = 1ull << 7;
        constexpr u64 MASK_FLUSH           = 1ull << 9;
        constexpr u64 MASK_TOPOLOGY        = 1ull << 10;
        constexpr u64 MASK_CONFIG_WCE      = 1ull << 11;
        constexpr u64 MASK_MQ              = 1ull << 12;
        constexpr u64 MASK_DISCARD         = 1ull << 13;
        constexpr u64 MASK_WRITE_ZEROES    = 1ull << 14;
        constexpr u64 MASK_LIFETIME        = 1ull << 15;
        constexpr u64 MASK_SECURE_ERASE    = 1ull << 16;
        constexpr u64 MASK_ZONED           = 1ull << 17;
    }  // namespace feature

    /**
     * @brief virtio 块设备配置空间布局.
     *
     * 字段语义与 virtio-blk 规范保持一致, 实际可用性取决于 feature 协商结果.
     */
    struct BlkConfig {
        le64 capacity;
        le32 size_max;
        le32 seg_max;
        struct BlkGeometry {
            le16 cylinders;
            u8 heads;
            u8 sectors;
        } geometry;
        le32 blk_size;
        struct BlkTopology {
            u8 physical_block_exp;
            u8 alignment_offset;
            le16 min_io_size;
            le32 opt_io_size;
        } topology;
        u8 writeback;
        u8 unused0;
        u16 num_queues;
        le32 max_discard_sectors;
        le32 max_discard_seg;
        le32 discard_sector_alignment;
        le32 max_write_zeroes_sectors;
        le32 max_write_zeroes_seg;
        u8 write_zeroes_may_unmap;
        u8 unused1[3];
        le32 max_secure_erase_sectors;
        le32 max_secure_erase_seg;
        le32 secure_erase_sector_alignment;
    };

    /**
     * @brief virtio 块设备请求头.
     */
    struct BlkReq {
        le32 type;
        le32 reserved;
        le64 sector;
    };

    /**
     * @brief virtio-blk 请求类型编号.
     */
    namespace reqtype {
        constexpr u32 IN          = 0;
        constexpr u32 OUT         = 1;
        constexpr u32 FLUSH       = 4;
        constexpr u32 DISCARD     = 11;
        constexpr u32 WRITE_ZEROS = 13;
    }  // namespace reqtype

    /**
     * @brief virtio-blk 请求完成状态码.
     */
    namespace reqstatus {
        constexpr u8 OK     = 0;
        constexpr u8 IOERR  = 1;
        constexpr u8 UNSUPP = 2;
    }  // namespace reqstatus

    /**
     * @brief virtio 块设备调试驱动.
     *
     * 当前版本仅实现同步轮询式读写原语与调试 hexdump 输出,
     * 暂不接入 `IBlockDeviceOps`.
     */
    class VirtioBlkDriver final : public VirtioDriverBase {
    public:
        /**
         * @brief 构造一个 virtio-blk 设备对象.
         */
        VirtioBlkDriver(DevRes res, ProbeInfo probe_info,
                        char *mmio_base) noexcept;
        /**
         * @brief 销毁 virtio-blk 设备对象并回收请求 DMA 缓冲区.
         */
        ~VirtioBlkDriver() noexcept override;

        /**
         * @brief 完成块设备配置解析、队列建立与通用初始化.
         */
        [[nodiscard]]
        Result<void> init() noexcept;

        /**
         * @brief 返回逻辑块大小.
         */
        [[nodiscard]]
        Result<size_t> block_sz() const noexcept;

        /**
         * @brief 返回逻辑块数量.
         */
        [[nodiscard]]
        Result<size_t> block_cnt() const noexcept;

        /**
         * @brief 同步读取若干逻辑块.
         */
        [[nodiscard]]
        Result<size_t> read_blocks(size_t lba, size_t block_count,
                                   void *buffer) noexcept;

        /**
         * @brief 同步写入若干逻辑块.
         */
        [[nodiscard]]
        Result<size_t> write_blocks(size_t lba, size_t block_count,
                                    const void *buffer) noexcept;

        /**
         * @brief 读入镜像偏移 `0x400~0x600` 并输出调试 hexdump.
         */
        [[nodiscard]]
        Result<void> __debug_print_blocks() noexcept;

    private:
        /**
         * @brief 从配置空间读取块设备关键字段.
         */
        [[nodiscard]]
        Result<void> load_config() noexcept;

        /**
         * @brief 通过 request virtqueue 提交一次同步读写请求.
         */
        [[nodiscard]]
        Result<size_t> submit_rw(u32 req_type, size_t sector, void *buffer,
                                 size_t bytes, bool device_writes) noexcept;

        /**
         * @brief 提交一次同步 flush 请求.
         */
        [[nodiscard]]
        Result<void> submit_flush() noexcept;

        static constexpr u16 REQUEST_QUEUE_INDEX = 0;
        static constexpr size_t DEFAULT_BLOCK_SIZE = 512;
        static constexpr size_t DEFAULT_SECTOR_SIZE = 512;

        BlkConfig _config{};
        VirtQueueLegacy *_request_queue = nullptr;
        DmaBuffer _request_header = {};
        DmaBuffer _status_byte    = {};
        size_t _block_size        = DEFAULT_BLOCK_SIZE;
        size_t _sector_size       = DEFAULT_SECTOR_SIZE;
        size_t _capacity_sectors  = 0;
    };

    /**
     * @brief virtio-blk 子工厂.
     */
    class VirtioBlkDriverFactory final : public IVirtioDeviceFactory {
    public:
        [[nodiscard]]
        DeviceType device_type() const noexcept override {
            return DeviceType::BLOCK;
        }

        /**
         * @brief 仅接管已识别为 virtio block 的节点.
         */
        [[nodiscard]]
        bool probe(const device::DeviceNode &node, device::DeviceModel &model,
                   const ProbeInfo &info) const noexcept override;

        /**
         * @brief 创建并初始化 `VirtioBlkDriver`.
         */
        [[nodiscard]]
        Result<driver::DriverBase *> create(
            const device::DeviceNode &node, device::DeviceModel &model,
            const ProbeInfo &info) const override;
    };

    /**
     * @brief 初始化 virtio-blk 子工厂注册.
     */
    void init_virtio_blk_factory() noexcept;
}  // namespace virtio
