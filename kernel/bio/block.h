/**
 * @file block.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 块设备接口
 * @version alpha-1.0.0
 * @date 2026-02-03
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <bio/request.h>
#include <driver/base.h>
#include <sus/rtti.h>
#include <sustcore/errcode.h>

#include <cstddef>

enum class BlockDeviceType { BASIC = 0, RAMDISK = 1 };

class IBlockDeviceOps : public RTTIBase<IBlockDeviceOps, BlockDeviceType> {
public:
    virtual ~IBlockDeviceOps() = default;
    /**
     * @brief 获得块大小
     *
     * @return size_t 块大小 (字节）
     */
    [[nodiscard]]
    virtual Result<size_t> block_sz(void) const = 0;
    /**
     * @brief 获得块数量
     *
     * @return size_t 块数量
     */
    [[nodiscard]]
    virtual Result<size_t> block_cnt(void) const = 0;
    /**
     * @brief 设备是否只读
     *
     * @return bool 是否只读
     */
    [[nodiscard]]
    virtual Result<bool> readonly(void) const {
        return false;
    }
    /**
     * @brief 获得设备总字节数
     *
     * @return size_t 设备总字节数
     */
    [[nodiscard]]
    virtual Result<size_t> total_bytes(void) const {
        auto block_sz_res = block_sz();
        propagate(block_sz_res);
        auto block_cnt_res = block_cnt();
        propagate(block_cnt_res);
        return block_sz_res.value() * block_cnt_res.value();
    }
    [[nodiscard]]
    virtual Result<void> bind_request_queue(
        util::nonnull<blk::BlockRequestQueue *> queue) = 0;
    [[nodiscard]]
    virtual Result<size_t> process_request(blk::BlockRequest &req) = 0;
    [[nodiscard]]
    virtual Result<void> run_request_loop() = 0;
};

namespace driver {
    class BlockDevice : public DriverBase, public IBlockDeviceOps {
    public:
        explicit BlockDevice(DevRes res) noexcept
            : DriverBase(std::move(res)) {}
        ~BlockDevice() override = default;
    };
}  // namespace driver

class RamDiskDevice : public IBlockDeviceOps {
private:
    void *D_base;
    size_t D_block_size;
    size_t D_block_count;
    blk::BlockRequestQueue *_queue = nullptr;

public:
    constexpr static BlockDeviceType IDENTIFIER = BlockDeviceType::BASIC;
    [[nodiscard]]
    BlockDeviceType type_id() const override
    {
        return IDENTIFIER;
    }
    ~RamDiskDevice() override = default;
    constexpr RamDiskDevice(void *base, size_t block_size, size_t block_count)
        : D_base(base), D_block_size(block_size), D_block_count(block_count) {}
    [[nodiscard]]
    Result<size_t> block_sz() const override
    {
        return D_block_size;
    }
    [[nodiscard]]
    Result<size_t> block_cnt() const override
    {
        return D_block_count;
    }
    [[nodiscard]]
    Result<void> bind_request_queue(
        util::nonnull<blk::BlockRequestQueue *> queue) override;
    [[nodiscard]]
    Result<size_t> process_request(blk::BlockRequest &req) override;
    [[nodiscard]]
    Result<void> run_request_loop() override;
    [[nodiscard]]
    constexpr void *base() const {
        return D_base;
    }

    [[nodiscard]]
    Result<size_t> read_blocks(lba_t lba, void *buf, size_t cnt);
    [[nodiscard]]
    Result<size_t> write_blocks(lba_t lba, const void *buf, size_t cnt);
    [[nodiscard]]
    Result<void> sync(void);
};
