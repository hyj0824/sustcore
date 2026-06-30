/**
 * @file virtio-blk.cpp
 * @author theflysong (song_of_the_fly@163.com)
 * @brief virtio 块设备驱动
 * @version alpha-1.0.0
 * @date 2026-06-10
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <device/resource.h>
#include <device/pci.h>
#include <bio/blk.h>
#include <driver/virtio/virtio-blk.h>
#include <logger.h>
#include <sus/raii.h>
#include <task/wait.h>
#include <vfs/vfs.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
    virtio::VirtioBlkDriverFactory g_virtio_blk_factory;
    bool g_virtio_blk_factory_initialized = false;

    /**
     * @brief 当前块驱动声明支持的 feature 集.
     *
     * 只保留当前调试读块路径真正会受益的特性位.
     */
    constexpr virtio::u64 SUPPORTED_BLK_FEATURES =
        virtio::feature::MASK_SIZE_MAX | virtio::feature::MASK_SEG_MAX |
        virtio::feature::MASK_GEOMETRY | virtio::feature::MASK_RO |
        virtio::feature::MASK_BLK_SIZE | virtio::feature::MASK_FLUSH |
        virtio::feature::MASK_TOPOLOGY | virtio::feature::MASK_CONFIG_WCE |
        virtio::feature::MASK_MQ | virtio::feature::MASK_DISCARD |
        virtio::feature::MASK_WRITE_ZEROES |
        virtio::common_feature::RING_INDIRECT_DESC |
        virtio::common_feature::RING_EVENT_IDX;

    constexpr size_t DEBUG_DUMP_OFFSET = 0x400;
    constexpr size_t DEBUG_DUMP_SIZE   = 0x200;
    constexpr size_t DEBUG_LINE_BYTES  = 16;

    /**
     * @brief 将一个字节转换为 hexdump 可显示字符.
     */
    [[nodiscard]]
    char printable_byte(unsigned char ch) noexcept {
        return isprint(ch) != 0 ? static_cast<char>(ch) : '.';
    }
}  // namespace

namespace virtio {
    VirtioBlkDriver::VirtioBlkDriver(DevRes res, ProbeInfo probe_info,
                                     util::owner<Transport *> transport) noexcept
        : VirtioDriverBase(std::move(res), probe_info, std::move(transport)),
          _config(),
          _request_queue(nullptr),
          _block_size(DEFAULT_BLOCK_SIZE),
          _sector_size(DEFAULT_SECTOR_SIZE),
          _capacity_sectors(0),
          _slot_wait_wd(wait::alloc_reason()) {}

    VirtioBlkDriver::~VirtioBlkDriver() noexcept {
        for (auto &slot : _slots) {
            free_dma_buffer(slot.request_header);
            free_dma_buffer(slot.status_byte);
        }
    }

    Result<void> VirtioBlkDriver::init() noexcept {
        // 先完成 virtio 通用初始化, 再加载块设备配置并创建 request 队列.
        auto init_res = begin_init(SUPPORTED_BLK_FEATURES);
        propagate(init_res);

        auto config_res = load_config();
        propagate(config_res);

        Result<VirtQueueLegacy *> queue_res = std::unexpected(
            ErrCode::NOT_SUPPORTED);
        if (_transport.get() != nullptr &&
            _transport->kind() == Transport::Kind::PCI)
        {
            queue_res = init_queue_modern(REQUEST_QUEUE_INDEX, 8);
        } else {
            queue_res = init_queue_legacy(REQUEST_QUEUE_INDEX, 8);
        }
        propagate(queue_res);
        _request_queue = queue_res.value();

        size_t slot_count = _request_queue->size / 3;
        if (slot_count == 0) {
            slot_count = 1;
        }

        _slots.clear();
        _slots.resize(slot_count);
        _slot_by_desc.assign(_request_queue->size, vring::INVALID_DESC);
        for (auto &slot : _slots) {
            auto header_res = alloc_dma_buffer(sizeof(BlkReq));
            propagate(header_res);
            slot.request_header = header_res.value();

            auto status_res = alloc_dma_buffer(sizeof(u8));
            propagate(status_res);
            slot.status_byte = status_res.value();
        }

        auto finish_res = finish_init();
        propagate(finish_res);

        loggers::DEVICE::INFO(
            "VirtioBlkDriver 初始化完成: node=%s capacity_sectors=%llu block_size=%llu",
            name(), static_cast<unsigned long long>(_capacity_sectors),
            static_cast<unsigned long long>(_block_size));
        void_return();
    }

    Result<void> VirtioBlkDriver::load_config() noexcept {
        // 当前只读取调试读块路径真正需要的关键字段, 避免过早依赖完整配置面.
        auto capacity_res = read_config_u64(offsetof(BlkConfig, capacity));
        propagate(capacity_res);
        _config.capacity = capacity_res.value();

        auto size_max_res = read_config_u32(offsetof(BlkConfig, size_max));
        propagate(size_max_res);
        _config.size_max = size_max_res.value();

        auto seg_max_res = read_config_u32(offsetof(BlkConfig, seg_max));
        propagate(seg_max_res);
        _config.seg_max = seg_max_res.value();

        auto blk_size_res = read_config_u32(offsetof(BlkConfig, blk_size));
        propagate(blk_size_res);
        _config.blk_size = blk_size_res.value();

        auto num_queues_res = read_config_u16(offsetof(BlkConfig, num_queues));
        propagate(num_queues_res);
        _config.num_queues = num_queues_res.value();

        _capacity_sectors = static_cast<size_t>(_config.capacity);
        _block_size =
            _config.blk_size != 0 ? static_cast<size_t>(_config.blk_size)
                                  : DEFAULT_BLOCK_SIZE;
        _sector_size = DEFAULT_SECTOR_SIZE;

        if (_block_size == 0 || (_block_size % _sector_size) != 0) {
            loggers::DEVICE::ERROR(
                "virtio-blk 非法块大小: node=%s block_size=%llu sector_size=%llu",
                name(), static_cast<unsigned long long>(_block_size),
                static_cast<unsigned long long>(_sector_size));
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        void_return();
    }

    Result<size_t> VirtioBlkDriver::block_sz() const noexcept {
        return _block_size;
    }

    Result<size_t> VirtioBlkDriver::block_cnt() const noexcept {
        if (_block_size == 0) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        const auto total_bytes =
            static_cast<unsigned long long>(_capacity_sectors) * _sector_size;
        return static_cast<size_t>(total_bytes / _block_size);
    }

    Result<bool> VirtioBlkDriver::readonly() const {
        return (negotiated_features() & feature::MASK_RO) != 0;
    }

    Result<void> VirtioBlkDriver::bind_request_queue(
        util::nonnull<blk::BlockRequestQueue *> queue) {
        if (_queue != nullptr && _queue != queue.get()) {
            unexpect_return(ErrCode::BUSY);
        }
        _queue = queue.get();
        void_return();
    }

    Result<void> VirtioBlkDriver::process_request(blk::BlockRequest &req) {
        if (_queue == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        auto slot_res = alloc_slot();
        propagate(slot_res);
        return submit_request_on_slot(slot_res.value(), req);
    }

    Result<void> VirtioBlkDriver::run_request_loop() {
        if (_queue == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        while (true) {
            auto reap_res = reap_completed_requests();
            propagate(reap_res);

            auto timeout_res = fail_timed_out_slots();
            propagate(timeout_res);

            auto finish_res = complete_finished_slots();
            propagate(finish_res);

            bool submitted_any = false;
            while (_inflight_count < _slots.size()) {
                auto req_res = _queue->try_dequeue();
                if (!req_res.has_value()) {
                    if (req_res.error() == ErrCode::ENTRY_NOT_FOUND) {
                        break;
                    }
                    if (req_res.error() == ErrCode::FUTURE_CANCLED) {
                        auto final_reap_res = reap_completed_requests();
                        propagate(final_reap_res);
                        auto final_timeout_res = fail_timed_out_slots();
                        propagate(final_timeout_res);
                        auto final_finish_res = complete_finished_slots();
                        propagate(final_finish_res);
                        if (_inflight_count == 0) {
                            void_return();
                        }
                        break;
                    }
                    propagate_return(req_res);
                }

                auto *req = req_res.value();
                auto mark_res = _queue->mark_processing(util::nnullforce(req));
                if (!mark_res.has_value()) {
                    auto complete_res = _queue->complete(
                        util::nnullforce(req),
                        std::unexpected(mark_res.error()));
                    propagate(complete_res);
                    continue;
                }

                auto process_res = process_request(*req);
                if (!process_res.has_value()) {
                    auto complete_res = _queue->complete(
                        util::nnullforce(req),
                        std::unexpected(process_res.error()));
                    propagate(complete_res);
                    continue;
                }
                submitted_any = true;
            }

            if (submitted_any || reap_res.value() != 0 || timeout_res.value() != 0 ||
                finish_res.value() != 0)
            {
                continue;
            }

            if (_inflight_count != 0) {
                auto wait_res = wait_for_inflight_progress();
                if (!wait_res.has_value() && wait_res.error() != ErrCode::TIMEOUT) {
                    propagate_return(wait_res);
                }
                continue;
            }

            auto req_res = _queue->wait_and_dequeue();
            if (!req_res.has_value()) {
                if (req_res.error() == ErrCode::FUTURE_CANCLED) {
                    break;
                }
                propagate_return(req_res);
            }

            auto *req = req_res.value();
            auto mark_res = _queue->mark_processing(util::nnullforce(req));
            if (!mark_res.has_value()) {
                auto complete_res = _queue->complete(
                    util::nnullforce(req), std::unexpected(mark_res.error()));
                propagate(complete_res);
                continue;
            }
            auto process_res = process_request(*req);
            if (!process_res.has_value()) {
                auto complete_res = _queue->complete(
                    util::nnullforce(req), std::unexpected(process_res.error()));
                propagate(complete_res);
            }
        }
        void_return();
    }

    Result<void> VirtioBlkDriver::mount(CapIdx devdir) noexcept {
        auto devno_res = blk::BlkManager::inst().find_device_id(this);
        propagate(devno_res);

        auto &vfs = VFS::inst();
        auto devfs_res = vfs.devfs();
        propagate(devfs_res);
        auto &devfs = *devfs_res.value();

        auto lookup_res = holder().lookup(devdir);
        propagate(lookup_res);
        auto &dircap = *lookup_res.value();

        auto mkres = vfs.mkfile(dircap, "vblk", flags::O_READ, holder());
        propagate(mkres);

        lookup_res = holder().lookup(mkres.value());
        propagate(lookup_res);
        auto &filecap = *lookup_res.value();
        auto link_res = devfs.link_block(filecap, devno_res.value());
        propagate(link_res);
        void_return();
    }

    Result<size_t> VirtioBlkDriver::submit_rw(u32 req_type, size_t sector,
                                              void *buffer, size_t bytes,
                                              bool device_writes) noexcept {
        // 先构造 virtio-blk 请求头与状态字节, 再把 header/data/status
        // 组织成一条三段描述符链提交到 request 队列.
        (void)req_type;
        (void)sector;
        (void)buffer;
        (void)bytes;
        (void)device_writes;
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    Result<size_t> VirtioBlkDriver::read_blocks(size_t lba, size_t block_count,
                                                void *buffer) noexcept {
        // 先做边界与对齐前置条件检查, 成功后按逻辑块换算为底层扇区请求.
        if (buffer == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (_block_size == 0 || _sector_size == 0 ||
            (_block_size % _sector_size) != 0)
        {
            loggers::DEVICE::ERROR(
                "virtio-blk read 参数非法: node=%s lba=%u block_count=%u block_size=%u sector_size=%u",
                name(), static_cast<unsigned>(lba),
                static_cast<unsigned>(block_count),
                static_cast<unsigned>(_block_size),
                static_cast<unsigned>(_sector_size));
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        const auto total_blocks = block_cnt();
        propagate(total_blocks);
        if (lba >= total_blocks.value() ||
            block_count > total_blocks.value() - lba)
        {
            loggers::DEVICE::ERROR(
                "virtio-blk read 越界: node=%s lba=%u block_count=%u total_blocks=%u",
                name(), static_cast<unsigned>(lba),
                static_cast<unsigned>(block_count),
                static_cast<unsigned>(total_blocks.value()));
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        const size_t sectors_per_block = _block_size / _sector_size;
        const size_t sector            = lba * sectors_per_block;
        const size_t bytes             = block_count * _block_size;
        return submit_rw(reqtype::IN, sector, buffer, bytes, true);
    }

    Result<size_t> VirtioBlkDriver::write_blocks(size_t lba, size_t block_count,
                                                 const void *buffer) noexcept {
        // 写路径与读路径共享同一套边界检查与扇区换算规则.
        if (buffer == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        if (_block_size == 0 || _sector_size == 0 ||
            (_block_size % _sector_size) != 0)
        {
            unexpect_return(ErrCode::INVALID_PARAM);
        }
        const auto total_blocks = block_cnt();
        propagate(total_blocks);
        if (lba >= total_blocks.value() ||
            block_count > total_blocks.value() - lba)
        {
            unexpect_return(ErrCode::OUT_OF_BOUNDARY);
        }

        const size_t sectors_per_block = _block_size / _sector_size;
        const size_t sector            = lba * sectors_per_block;
        const size_t bytes             = block_count * _block_size;
        return submit_rw(reqtype::OUT, sector, const_cast<void *>(buffer), bytes,
                         false);
    }

    Result<void> VirtioBlkDriver::submit_flush() noexcept {
        unexpect_return(ErrCode::NOT_SUPPORTED);
    }

    Result<size_t> VirtioBlkDriver::alloc_slot() noexcept {
        auto completed_res = complete_finished_slots();
        propagate(completed_res);
        for (size_t i = 0; i < _slots.size(); ++i) {
            auto &slot = _slots[i];
            if (!slot.in_use) {
                slot.in_use        = true;
                slot.completed     = false;
                slot.timed_out     = false;
                slot.req           = nullptr;
                slot.desc_idx      = vring::INVALID_DESC;
                slot.success_bytes = 0;
                slot.deadline_ns   = 0;
                slot.result        = std::unexpected(ErrCode::FUTURE_PENDING);
                return i;
            }
        }
        unexpect_return(ErrCode::BUSY);
    }

    Result<void> VirtioBlkDriver::submit_request_on_slot(
        size_t slot_idx, blk::BlockRequest &req) noexcept {
        if (_request_queue == nullptr || slot_idx >= _slots.size()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto &slot = _slots[slot_idx];
        if (!slot.in_use || slot.request_header.kaddr == nullptr ||
            slot.status_byte.kaddr == nullptr)
        {
            unexpect_return(ErrCode::FAILURE);
        }

        u32 req_type = 0;
        size_t sector = 0;
        size_t bytes = 0;
        bool device_writes = false;

        switch (req.op) {
            case blk::BlockOp::READ:
            case blk::BlockOp::WRITE: {
                if (req.buffer == nullptr) {
                    release_slot(slot_idx);
                    unexpect_return(ErrCode::NULLPTR);
                }
                if (_block_size == 0 || _sector_size == 0 ||
                    (_block_size % _sector_size) != 0)
                {
                    release_slot(slot_idx);
                    unexpect_return(ErrCode::INVALID_PARAM);
                }
                auto total_blocks_res = block_cnt();
                if (!total_blocks_res.has_value()) {
                    auto err = total_blocks_res.error();
                    release_slot(slot_idx);
                    unexpect_return(err);
                }
                if (req.lba >= total_blocks_res.value() ||
                    req.block_count > total_blocks_res.value() - req.lba)
                {
                    release_slot(slot_idx);
                    unexpect_return(ErrCode::OUT_OF_BOUNDARY);
                }
                const size_t sectors_per_block = _block_size / _sector_size;
                req_type = req.op == blk::BlockOp::READ ? reqtype::IN
                                                        : reqtype::OUT;
                sector = req.lba * sectors_per_block;
                bytes = req.block_count * _block_size;
                device_writes = req.op == blk::BlockOp::READ;
                break;
            }
            case blk::BlockOp::FLUSH:
                req_type = reqtype::FLUSH;
                sector = 0;
                bytes = 0;
                device_writes = false;
                break;
            default:
                release_slot(slot_idx);
                unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto *header = static_cast<BlkReq *>(slot.request_header.kaddr);
        auto *status = static_cast<u8 *>(slot.status_byte.kaddr);
        header->type     = req_type;
        header->reserved = 0;
        header->sector   = sector;
        *status          = 0xFF;

        std::vector<vring::BufferView> bufs;
        bufs.reserve(bytes == 0 ? 2 : 3);
        bufs.push_back(vring::BufferView{
            .paddr    = slot.request_header.paddr,
            .size     = sizeof(BlkReq),
            .writable = false,
        });
        if (bytes != 0) {
            bufs.push_back(vring::BufferView{
                .paddr    = convert_pointer(static_cast<char *>(req.buffer)),
                .size     = bytes,
                .writable = device_writes,
            });
        }
        bufs.push_back(vring::BufferView{
            .paddr    = slot.status_byte.paddr,
            .size     = sizeof(u8),
            .writable = true,
        });

        auto head_res = queue_add_chain_legacy(*_request_queue, bufs);
        if (!head_res.has_value()) {
            auto err = head_res.error();
            release_slot(slot_idx);
            unexpect_return(err);
        }

        auto submit_guard = util::Guard([this, slot_idx]() {
            auto &guard_slot = _slots[slot_idx];
            if (guard_slot.desc_idx != vring::INVALID_DESC) {
                (void)queue_free_chain_legacy(*_request_queue, guard_slot.desc_idx);
            }
            (void)release_slot(slot_idx);
        });

        slot.req           = &req;
        slot.desc_idx      = head_res.value();
        slot.success_bytes = bytes;
        slot.deadline_ns   = now_ns() + REQUEST_TIMEOUT_NS;
        slot.result        = std::unexpected(ErrCode::FUTURE_PENDING);
        if (slot.desc_idx < _slot_by_desc.size()) {
            _slot_by_desc[slot.desc_idx] = static_cast<u16>(slot_idx);
        }

        auto submit_res = queue_submit_legacy(*_request_queue, slot.desc_idx);
        propagate(submit_res);
        auto notify_res = queue_notify_legacy(*_request_queue);
        propagate(notify_res);

        ++_inflight_count;
        submit_guard.release();
        void_return();
    }

    Result<size_t> VirtioBlkDriver::reap_completed_requests() noexcept {
        if (_request_queue == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }

        size_t count = 0;
        while (true) {
            auto can_pop_res = queue_can_pop_legacy(*_request_queue);
            propagate(can_pop_res);
            if (!can_pop_res.value()) {
                break;
            }

            auto used_res = queue_pop_used_legacy(*_request_queue);
            propagate(used_res);
            ++count;

            const u16 desc_idx = static_cast<u16>(used_res.value().id);
            if (desc_idx >= _slot_by_desc.size()) {
                loggers::DEVICE::ERROR(
                    "virtio-blk used id 越界: node=%s used_id=%u desc_slots=%u",
                    name(), static_cast<unsigned>(desc_idx),
                    static_cast<unsigned>(_slot_by_desc.size()));
                continue;
            }

            const u16 slot_idx = _slot_by_desc[desc_idx];
            _slot_by_desc[desc_idx] = vring::INVALID_DESC;
            if (slot_idx == vring::INVALID_DESC || slot_idx >= _slots.size()) {
                loggers::DEVICE::ERROR(
                    "virtio-blk used id 未映射 slot: node=%s used_id=%u slot=%u",
                    name(), static_cast<unsigned>(desc_idx),
                    static_cast<unsigned>(slot_idx));
                continue;
            }

            auto &slot = _slots[slot_idx];
            if (!slot.in_use || slot.desc_idx != desc_idx) {
                loggers::DEVICE::ERROR(
                    "virtio-blk slot 状态非法: node=%s slot=%u desc=%u in_use=%d slot_desc=%u",
                    name(), static_cast<unsigned>(slot_idx),
                    static_cast<unsigned>(desc_idx), static_cast<int>(slot.in_use),
                    static_cast<unsigned>(slot.desc_idx));
                continue;
            }

            auto free_res = queue_free_chain_legacy(*_request_queue, desc_idx);
            if (!free_res.has_value()) {
                loggers::DEVICE::ERROR(
                    "virtio-blk 释放 used 链失败: node=%s slot=%u desc=%u err=%s",
                    name(), static_cast<unsigned>(slot_idx),
                    static_cast<unsigned>(desc_idx),
                    to_cstring(free_res.error()));
                slot.result    = std::unexpected(free_res.error());
                slot.completed = true;
                slot.desc_idx  = vring::INVALID_DESC;
                continue;
            }

            auto *status = static_cast<u8 *>(slot.status_byte.kaddr);
            if (status == nullptr) {
                slot.result = std::unexpected(ErrCode::NULLPTR);
            } else if (*status != reqstatus::OK) {
                loggers::DEVICE::ERROR(
                    "virtio-blk 请求失败: node=%s slot=%u desc=%u status=%u",
                    name(), static_cast<unsigned>(slot_idx),
                    static_cast<unsigned>(desc_idx),
                    static_cast<unsigned>(*status));
                slot.result = std::unexpected(ErrCode::IO_ERROR);
            } else {
                slot.result = slot.success_bytes;
            }

            slot.completed = true;
            slot.desc_idx  = vring::INVALID_DESC;
        }

        if (count != 0) {
            auto wake_res = wait::wake_all(_slot_wait_wd);
            if (!wake_res.has_value()) {
                loggers::DEVICE::ERROR("virtio-blk 唤醒 slot waiter 失败: node=%s err=%s",
                                       name(), to_cstring(wake_res.error()));
            }
        }
        return count;
    }

    Result<size_t> VirtioBlkDriver::complete_finished_slots() noexcept {
        size_t count = 0;
        for (size_t i = 0; i < _slots.size(); ++i) {
            auto &slot = _slots[i];
            if (!slot.in_use || !slot.completed || slot.req == nullptr) {
                continue;
            }

            auto *req = slot.req;
            auto result = std::move(slot.result);
            auto complete_res = _queue->complete(util::nnullforce(req),
                                                 std::move(result));
            propagate(complete_res);
            auto release_res = release_slot(i);
            propagate(release_res);
            ++count;
        }
        return count;
    }

    Result<size_t> VirtioBlkDriver::fail_timed_out_slots() noexcept {
        const size_t now = now_ns();
        size_t count = 0;
        for (size_t i = 0; i < _slots.size(); ++i) {
            auto &slot = _slots[i];
            if (!slot.in_use || slot.completed || slot.req == nullptr ||
                slot.desc_idx == vring::INVALID_DESC)
            {
                continue;
            }
            if (slot.deadline_ns > now) {
                continue;
            }

            loggers::DEVICE::ERROR(
                "virtio-blk 请求超时: node=%s slot=%u lba=%lu cnt=%lu desc=%u",
                name(), static_cast<unsigned>(i),
                static_cast<unsigned long>(slot.req->lba),
                static_cast<unsigned long>(slot.req->block_count),
                static_cast<unsigned>(slot.desc_idx));

            if (slot.desc_idx < _slot_by_desc.size()) {
                _slot_by_desc[slot.desc_idx] = vring::INVALID_DESC;
            }
            auto free_res = queue_free_chain_legacy(*_request_queue, slot.desc_idx);
            if (!free_res.has_value()) {
                loggers::DEVICE::ERROR(
                    "virtio-blk 超时链释放失败: node=%s slot=%u desc=%u err=%s",
                    name(), static_cast<unsigned>(i),
                    static_cast<unsigned>(slot.desc_idx),
                    to_cstring(free_res.error()));
            }
            slot.desc_idx  = vring::INVALID_DESC;
            slot.result    = std::unexpected(ErrCode::TIMEOUT);
            slot.completed = true;
            slot.timed_out = true;
            ++count;
        }

        if (count != 0) {
            auto wake_res = wait::wake_all(_slot_wait_wd);
            if (!wake_res.has_value()) {
                loggers::DEVICE::ERROR("virtio-blk 唤醒超时 waiter 失败: node=%s err=%s",
                                       name(), to_cstring(wake_res.error()));
            }
        }
        return count;
    }

    Result<bool> VirtioBlkDriver::has_used_completion() noexcept {
        if (_request_queue == nullptr) {
            unexpect_return(ErrCode::NULLPTR);
        }
        return queue_can_pop_legacy(*_request_queue);
    }

    Result<bool> VirtioBlkDriver::has_inflight_timeout() noexcept {
        const size_t now = now_ns();
        for (const auto &slot : _slots) {
            if (slot.in_use && !slot.completed && slot.req != nullptr &&
                slot.deadline_ns <= now)
            {
                return true;
            }
        }
        return false;
    }

    Result<void> VirtioBlkDriver::wait_for_inflight_progress() noexcept {
        auto wait_res = timeout_wait_event(
            _slot_wait_wd, REQUEST_POLL_WAIT_NS,
            ({
                auto used_res = has_used_completion();
                auto timeout_res = has_inflight_timeout();
                used_res.has_value() && timeout_res.has_value() &&
                    (used_res.value() || timeout_res.value());
            }));
        if (!wait_res.has_value()) {
            propagate_return(wait_res);
        }
        void_return();
    }

    Result<void> VirtioBlkDriver::release_slot(size_t slot_idx) noexcept {
        if (slot_idx >= _slots.size()) {
            unexpect_return(ErrCode::INVALID_PARAM);
        }

        auto &slot = _slots[slot_idx];
        if (!slot.in_use) {
            void_return();
        }
        if (_inflight_count != 0 && slot.req != nullptr) {
            --_inflight_count;
        }
        if (slot.desc_idx != vring::INVALID_DESC &&
            slot.desc_idx < _slot_by_desc.size())
        {
            _slot_by_desc[slot.desc_idx] = vring::INVALID_DESC;
        }
        slot.req           = nullptr;
        slot.desc_idx      = vring::INVALID_DESC;
        slot.success_bytes = 0;
        slot.deadline_ns   = 0;
        slot.result        = std::unexpected(ErrCode::FUTURE_PENDING);
        slot.in_use        = false;
        slot.completed     = false;
        slot.timed_out     = false;
        void_return();
    }

    size_t VirtioBlkDriver::now_ns() const noexcept {
        auto *time_keeper =
            env::hart_ctx != nullptr ? env::hart_ctx->time_keeper() : nullptr;
        if (time_keeper == nullptr || time_keeper->source() == nullptr) {
            return 0;
        }
        return static_cast<size_t>(
            time_keeper->source()->to_ns(time_keeper->source()->now())
                .to_nanoseconds());
    }

    Result<void> VirtioBlkDriver::__debug_print_blocks() noexcept {
        // 先按整块读取覆盖目标窗口的最小块范围, 再只截取需要对比的字节区间.
        const auto total_blocks = block_cnt();
        propagate(total_blocks);
        if (total_blocks.value() == 0) {
            unexpect_return(ErrCode::ENTRY_NOT_FOUND);
        }

        const size_t first_block = DEBUG_DUMP_OFFSET / _block_size;
        const size_t end_offset  = DEBUG_DUMP_OFFSET + DEBUG_DUMP_SIZE;
        const size_t last_block  = (end_offset + _block_size - 1) / _block_size;
        const size_t block_count = last_block - first_block;

        std::vector<u8> blocks(block_count * _block_size, 0);
        auto read_res = read_blocks(first_block, block_count, blocks.data());
        propagate(read_res);

        const size_t window_offset = DEBUG_DUMP_OFFSET - first_block * _block_size;
        const u8 *window           = blocks.data() + window_offset;
        loggers::DEVICE::DEBUG(
            "virtio-blk 调试窗口: node=%s image_offset=[0x%llx,0x%llx)",
            name(), static_cast<unsigned long long>(DEBUG_DUMP_OFFSET),
            static_cast<unsigned long long>(DEBUG_DUMP_OFFSET + DEBUG_DUMP_SIZE));

        // 最后按 hexdump 风格输出固定窗口, 便于与宿主机镜像逐行对比.
        std::array<char, 128> line{};
        for (size_t line_offset = 0; line_offset < DEBUG_DUMP_SIZE;
             line_offset += DEBUG_LINE_BYTES)
        {
            int pos = snprintf(
                line.data(), line.size(), "%08llx  ",
                static_cast<unsigned long long>(DEBUG_DUMP_OFFSET + line_offset));
            for (size_t i = 0; i < DEBUG_LINE_BYTES; ++i) {
                pos += snprintf(
                    line.data() + pos, line.size() - static_cast<size_t>(pos),
                    "%02x%s", static_cast<unsigned>(window[line_offset + i]),
                    i == 7 ? "  " : " ");
            }
            pos += snprintf(line.data() + pos,
                            line.size() - static_cast<size_t>(pos), " |");
            for (size_t i = 0; i < DEBUG_LINE_BYTES; ++i) {
                line[static_cast<size_t>(pos)] =
                    printable_byte(window[line_offset + i]);
                ++pos;
            }
            line[static_cast<size_t>(pos)] = '|';
            ++pos;
            line[static_cast<size_t>(pos)]   = '\0';
            loggers::DEVICE::DEBUG("%s", line.data());
        }
        void_return();
    }

    bool VirtioBlkDriverFactory::probe(const device::DeviceNode &node,
                                       device::DeviceModel &model,
                                       const ProbeInfo &info) const noexcept {
        (void)node;
        (void)model;
        return info.valid &&
               static_cast<DeviceType>(info.device_id) == DeviceType::BLOCK;
    }

    Result<driver::DriverBase *> VirtioBlkDriverFactory::create(
        const device::DeviceNode &node, device::DeviceModel &model,
        const ProbeInfo &info) const {
        (void)model;
        // 按 transport 构造具体访问后端, 然后再创建统一 virtio-blk 驱动对象.
        auto virqs = device::DevResManager::get_virq_resource(node);
        auto mmios = device::DevResManager::get_mmio_resource(node);
        util::owner<Transport *> transport =
            util::owner<Transport *>(nullptr);
        if (node.platform() == device::DevicePlatform::FDT) {
            if (mmios.empty()) {
                unexpect_return(ErrCode::ENTRY_NOT_FOUND);
            }

            auto *mmio = mmios.front().get();
            if (mmio == nullptr) {
                unexpect_return(ErrCode::NULLPTR);
            }
            auto map_res = device::MMIOManager::inst().map_to_kernel(*mmio);
            propagate(map_res);
            transport = util::owner<Transport *>(
                new TransportMMIO(info, *mmio, map_res.value().as<char>()));
        } else if (node.platform() == device::DevicePlatform::PCI) {
            auto *pci_node = node.as<pci::PCIDeviceNode>();
            if (pci_node == nullptr) {
                unexpect_return(ErrCode::INVALID_PARAM);
            }
            transport = util::owner<Transport *>(
                new TransportPCI(*pci_node, info));
        } else {
            unexpect_return(ErrCode::NOT_SUPPORTED);
        }

        auto *driver = new VirtioBlkDriver(
            driver::DriverBase::DevRes(node, std::move(virqs), std::move(mmios)),
            info, std::move(transport));
        if (driver == nullptr) {
            unexpect_return(ErrCode::OUT_OF_MEMORY);
        }

        auto init_res = driver->init();
        if (!init_res.has_value()) {
            delete driver;
            propagate_return(init_res);
        }

        auto dump_res = driver->__debug_print_blocks();
        if (!dump_res.has_value()) {
            loggers::DEVICE::ERROR(
                "virtio-blk 调试窗口读取失败: node=%s err=%s", node.name(),
                to_cstring(dump_res.error()));
        }

        if (!blk::BlkManager::initialized()) {
            loggers::DEVICE::ERROR("virtio-blk 创建失败: BlkManager 未初始化");
            delete driver;
            unexpect_return(ErrCode::FAILURE);
        }

        auto devno_res = blk::BlkManager::inst().register_device(
            util::nnullforce(static_cast<IBlockDeviceOps *>(driver)));
        if (!devno_res.has_value()) {
            delete driver;
            propagate_return(devno_res);
        }
        loggers::DEVICE::INFO("virtio-blk 已接入块层: node=%s devno=%u",
                              node.name(),
                              static_cast<unsigned>(devno_res.value()));
        return static_cast<driver::DriverBase *>(driver);
    }

    void init_virtio_blk_factory() noexcept {
        // 子工厂注册必须保持幂等, 避免 FDT provider 重复初始化时二次登记.
        if (g_virtio_blk_factory_initialized) {
            return;
        }
        g_virtio_blk_factory_initialized = register_factory(g_virtio_blk_factory);
    }
}  // namespace virtio
