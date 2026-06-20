/**
 * @file block.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 块设备接口
 * @version alpha-1.0.0
 * @date 2026-02-03
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <bio/block.h>
#include <logger.h>

#include <cstring>

Result<void> RamDiskDevice::bind_request_queue(
    util::nonnull<blk::BlockRequestQueue *> queue) {
    _queue = queue.get();
    void_return();
}

Result<void> RamDiskDevice::process_request(blk::BlockRequest &req) {
    Result<size_t> result = std::unexpected(ErrCode::INVALID_PARAM);
    switch (req.op) {
        case blk::BlockOp::READ:
            result = read_blocks(req.lba, req.buffer, req.block_count);
            if (result.has_value()) {
                result = result.value() * D_block_size;
            }
            break;
        case blk::BlockOp::WRITE:
            result = write_blocks(req.lba, req.buffer, req.block_count);
            if (result.has_value()) {
                result = result.value() * D_block_size;
            }
            break;
        case blk::BlockOp::FLUSH: {
            auto sync_res = sync();
            if (sync_res.has_value()) {
                result = 0;
            } else {
                result = std::unexpected(sync_res.error());
            }
            break;
        }
        default: break;
    }
    if (!result.has_value()) {
        loggers::SUSTCORE::ERROR(
            "RamDisk request failed: op=%u lba=%u cnt=%u block_size=%u blocks=%u err=%s",
            static_cast<unsigned>(req.op), static_cast<unsigned>(req.lba),
            static_cast<unsigned>(req.block_count),
            static_cast<unsigned>(D_block_size),
            static_cast<unsigned>(D_block_count),
            to_cstring(result.error()));
    }
    if (_queue == nullptr) {
        unexpect_return(ErrCode::NULLPTR);
    }
    auto complete_res =
        _queue->complete(util::nnullforce(&req), std::move(result));
    propagate(complete_res);
    void_return();
}

Result<void> RamDiskDevice::run_request_loop() {
    if (_queue == nullptr) {
        unexpect_return(ErrCode::NULLPTR);
    }

    while (true) {
        auto req_res = _queue->wait_and_dequeue();
        if (!req_res.has_value()) {
            if (req_res.error() == ErrCode::FUTURE_CANCLED) {
                break;
            }
            propagate_return(req_res);
        }

        auto *req     = req_res.value();
        auto mark_res = _queue->mark_processing(util::nnullforce(req));
        propagate(mark_res);
        auto process_res = process_request(*req);
        propagate(process_res);
    }
    void_return();
}

Result<size_t> RamDiskDevice::read_blocks(lba_t lba, void *buf, size_t cnt) {
    if (buf == nullptr && cnt != 0) {
        unexpect_return(ErrCode::NULLPTR);
    }
    // 读 0 块的情景下, lba可以与块数量相等
    if (lba == D_block_count && cnt == 0) {
        return 0;
    }
    // 读块时, lba必须小于块数量
    if (lba >= D_block_count) {
        unexpect_return(ErrCode::OUT_OF_BOUNDARY);
    }
    if (cnt == 0) {
        return 0;
    }
    size_t to_read = cnt;
    // 如果请求的块范围超出设备边界, 只读取能容纳的块数量
    if (lba + cnt > D_block_count) {
        to_read = D_block_count - lba;
    }
    void *src = (char *)D_base + lba * D_block_size;
    memcpy(buf, src, to_read * D_block_size);
    return to_read;
}

Result<size_t> RamDiskDevice::write_blocks(lba_t lba, const void *buf,
                                           size_t cnt) {
    if (buf == nullptr && cnt != 0) {
        unexpect_return(ErrCode::NULLPTR);
    }
    if (lba == D_block_count && cnt == 0) {
        return 0;
    }
    if (lba >= D_block_count) {
        unexpect_return(ErrCode::OUT_OF_BOUNDARY);
    }
    if (cnt == 0) {
        return 0;
    }
    size_t to_write = cnt;
    if (lba + cnt > D_block_count) {
        to_write = D_block_count - lba;
    }
    void *dst = (char *)D_base + lba * D_block_size;
    memcpy(dst, buf, to_write * D_block_size);
    return to_write;
}

Result<void> RamDiskDevice::sync(void) {
    // RamDisk不需要同步
    void_return();
}
