/**
 * @file buffer.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 块缓存
 * @version alpha-1.0.0
 * @date 2026-06-06
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <bio/block.h>
#include <task/wait.h>
#include <string.h>
#include <sus/nonnull.h>
#include <sus/owner.h>
#include <sus/types.h>
#include <sustcore/errcode.h>

#include <cassert>
#include <unordered_map>

namespace blk {
    class BufferCache;

    /**
     * @brief 块缓存描述符
     *
     * 每个 Buffer 代表一个设备块的缓存副本
     * 这实际上是一个纯数据结构, 在任何情况下都不应该直接操作 Buffer 对象,
     * 而应该通过 BufferHandler 来管理其生命周期, 进行读写操作
     */
    struct Buffer {
        // 标识
        size_t blkno;  // 块号

        // 数据
        char *data;  // 指向块数据的指针(大小等于块大小)

        // 状态与并发控制
        bool dirty;  // 是否需要写回磁盘
        bool valid;  // data 中是否包含有效数据(读完成标志)
        bool inflight;  // 是否存在未完成请求
        int refcnt;  // 引用计数

        // 这里以后可能会有一个锁

        // 引用计数维护
        constexpr void keep() noexcept {
            refcnt++;
        }
        constexpr void release() noexcept {
            // TODO: 改为原子操作
            if (refcnt > 0) {
                refcnt--;
            }
        }
    };

    /**
     * @brief 块句柄, 维护了块的引用计数
     *
     */
    class BufferHandler {
    private:
        Buffer *_buf = nullptr;  // 指向块缓存的指针
        BufferCache *_cache = nullptr;  // 指向所属缓存的指针, 用于在析构时释放缓存

        constexpr void keep() noexcept {
            if (_buf != nullptr) {
                _buf->keep();
            }
        }

        constexpr void release() noexcept {
            if (_buf != nullptr) {
                _buf->release();
            }
        }

    public:
        constexpr BufferHandler(std::nullptr_t) noexcept = delete;

        constexpr BufferHandler(util::nonnull<Buffer *> buf,
                                BufferCache &cache) noexcept
            : _buf(buf), _cache(&cache) {
            keep();
        }

        constexpr void cleanup() noexcept {
            release();
            _buf = nullptr;
            _cache = nullptr;
        }

        constexpr ~BufferHandler() noexcept {
            cleanup();
        }

        constexpr BufferHandler(const BufferHandler &other) noexcept
            : _buf(other._buf), _cache(other._cache) {
            keep();
        }

        constexpr BufferHandler &operator=(
            const BufferHandler &other) noexcept {
            if (this != &other) {
                cleanup();
                _buf = other._buf;
                _cache = other._cache;
                keep();
            }
            return *this;
        }

        constexpr BufferHandler(BufferHandler &&other) noexcept
            : _buf(other._buf), _cache(other._cache) {
            other._buf = nullptr;
            other._cache = nullptr;
        }

        constexpr BufferHandler &operator=(BufferHandler &&other) noexcept {
            if (this != &other) {
                cleanup();
                _buf       = other._buf;
                _cache     = other._cache;
                other._buf = nullptr;
                other._cache = nullptr;
            }
            return *this;
        }

        // dangerous, think twice before using it
        [[nodiscard]]
        constexpr Buffer *get() const noexcept {
            return _buf;
        }

        [[nodiscard]]
        size_t blksz() const;

        [[nodiscard]]
        constexpr size_t blkno() const {
            return _buf->blkno;
        }

        [[nodiscard]]
        size_t devno() const;

        [[nodiscard]]
        constexpr size_t refcnt() const {
            return _buf->refcnt;
        }

        [[nodiscard]]
        constexpr bool is_dirty() const {
            return _buf->dirty;
        }

        [[nodiscard]]
        constexpr bool is_valid() const {
            return _buf->valid;
        }

        // 直接写入数据到缓存
        size_t write(size_t offset, const void *data, size_t len) {
            if (_buf == nullptr || _buf->data == nullptr) {
                return 0;
            }
            if (offset >= blksz()) {
                return 0;
            }
            if (offset + len > blksz()) {
                // 超出块大小限制, 只写入能容纳的数据
                len = blksz() - offset;
            }

            // TODO: 加锁
            memcpy(_buf->data + offset, data, len);
            _buf->dirty = true;
            return len;
        }

        // 直接从缓存读取数据
        size_t read(size_t offset, void *buf, size_t len) {
            if (_buf == nullptr || _buf->data == nullptr) {
                return 0;
            }
            if (offset >= blksz()) {
                return 0;
            }
            if (offset + len > blksz()) {
                // 超出块大小限制, 只读取能容纳的数据
                len = blksz() - offset;
            }
            // TODO: 加锁
            memcpy(buf, _buf->data + offset, len);
            return len;
        }

        // dangersous, make sure buf is not null and buflen is equal to block
        // size
        void writeblk(const void *buf, size_t buflen) {
            assert(buf != nullptr);
            assert(buflen == blksz());
            // TODO: 加锁
            memcpy(_buf->data, buf, blksz());
            _buf->dirty = true;
        }

        // dangersous, make sure buf is not null and buflen is equal to block
        // size
        void readblk(void *buf, size_t buflen) {
            assert(buf != nullptr);
            assert(buflen == blksz());
            // TODO: 加锁
            memcpy(buf, _buf->data, blksz());
        }

        friend class BufferCache;
    };

    // Buffer Cache
    class BufferCache {
    public:
        BufferCache(size_t devno, size_t blksz);
        ~BufferCache();

        // 禁止拷贝/复制
        BufferCache(const BufferCache &) = delete;
        BufferCache &operator=(const BufferCache &) = delete;
        BufferCache(BufferCache &&) = delete;
        BufferCache &operator=(BufferCache &&) = delete;
    private:
        size_t _devno;
        size_t _blksz;
        static constexpr size_t MAX_CACHE_SIZE = 8192;
        util::owner<Buffer *> _buffers[MAX_CACHE_SIZE];
        std::unordered_map<lba_t, size_t>
            _mapping;  // 块位置到缓存索引的映射
        [[nodiscard]]
        FutureResult<void> make_void_future(Result<void> result);
        [[nodiscard]]
        FutureResult<BufferHandler> make_handler_future(
            Result<BufferHandler> result);
        [[nodiscard]]
        Result<void> clear_slot(size_t idx);
        [[nodiscard]]
        Result<size_t> find_free();
        [[nodiscard]]
        Result<size_t> find_buffer(lba_t blkno);
        // 确保指定块号的缓存存在, 不存在则创建
        [[nodiscard]]
        Result<size_t> ensure_buffer(lba_t blkno);
    public:
        // 获取块大小
        [[nodiscard]]
        constexpr size_t blksz() const {
            return _blksz;
        }
        // 获取设备号
        [[nodiscard]]
        constexpr size_t devno() const {
            return _devno;
        }

        // 同步

        // 同步指定块到设备
        [[nodiscard]]
        FutureResult<void> sync(BufferHandler &handler);

        // 同步所有脏块到设备
        [[nodiscard]]
        FutureResult<void> sync_all();

        /**
         * @brief 释放缓存
         *
         * 1. 将所有的脏块写回设备
         * 2. 将所有引用计数为0的块从缓存中移除
         *
         */
        [[nodiscard]]
        FutureResult<void> tidy();

        //
        [[nodiscard]]
        FutureResult<BufferHandler> get_buffer_async(
            lba_t blkno);  // 获取块缓存
    };

    inline size_t BufferHandler::blksz() const {
        assert(_cache != nullptr);
        return _cache->blksz();
    }

    inline size_t BufferHandler::devno() const {
        assert(_cache != nullptr);
        return _cache->devno();
    }
}  // namespace blk
