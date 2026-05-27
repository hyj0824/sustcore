/**
 * @file uaccess.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 用户态缓冲区访问封装
 * @version alpha-1.0.0
 * @date 2026-05-15
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <env.h>
#include <logger.h>
#include <mem/userspace.h>
#include <mem/vma.h>
#include <sus/owner.h>
#include <sustcore/addr.h>
#include <syscall/syscall.h>

#include <cstddef>
#include <cstring>

namespace syscall {
    /**
     * @brief 将用户空间缓冲区映射为内核可读写缓存.
     */
    class UBuffer {
    private:
        VirAddr _uaddr;             // 用户空间地址
        util::owner<char *> _kbuf;  // 内核空间缓冲区地址
        size_t _len;                // 数据长度
        TaskMemoryManager *_tmm;    // 用户空间内存管理器
    public:
        /**
         * @brief 构造一个用户缓冲区代理.
         *
         * @param uaddr 用户空间地址.
         * @param len 缓冲区长度.
         */
        UBuffer(VirAddr uaddr, size_t len)
            : _uaddr(uaddr), _len(len), _tmm(nullptr) {
            auto current_tcb_res = syscall::current_tcb();
            if (current_tcb_res.has_value() &&
                current_tcb_res.value()->task != nullptr)
            {
                _tmm = current_tcb_res.value()->task->tmm.get();
            } else {
                _tmm = env::inst().tmm();
            }
            // 分配内核空间缓冲区
            _kbuf = util::owner(new char[_len]);
        }

        UBuffer(UBuffer &)            = delete;
        UBuffer &operator=(UBuffer &) = delete;

        /**
         * @brief 移动构造用户缓冲区代理.
         *
         * @param other 被移动对象.
         */
        UBuffer(UBuffer &&other) noexcept
            : _uaddr(other._uaddr),
              _kbuf(std::move(other._kbuf)),
              _len(other._len),
              _tmm(other._tmm) {
            other._uaddr = VirAddr::null;
            other._kbuf  = util::owner<char *>(nullptr);
            other._len   = 0;
            other._tmm   = nullptr;
        }

        /**
         * @brief 释放内核缓冲区资源.
         */
        void cleanup() noexcept {
            if (_kbuf != nullptr) {
                loggers::SYSCALL::DEBUG("释放内核缓冲区: %p", _kbuf.get());
                delete[] _kbuf;
                _kbuf = util::owner<char *>(nullptr);
            }
        }

        /**
         * @brief 移动赋值用户缓冲区代理.
         *
         * @param other 被移动对象.
         * @return UBuffer& 当前对象.
         */
        UBuffer &operator=(UBuffer &&other) noexcept {
            if (this != &other) {
                cleanup();
                _uaddr = other._uaddr;
                _kbuf  = std::move(other._kbuf);
                _len   = other._len;
                _tmm   = other._tmm;

                other._kbuf  = util::owner<char *>(nullptr);
                other._uaddr = VirAddr::null;
                other._len   = 0;
                other._tmm   = nullptr;
            }
            return *this;
        }

        /**
         * @brief 析构并释放内部缓冲.
         */
        ~UBuffer() noexcept {
            cleanup();
        }

        /**
         * @brief 将用户空间数据复制到内核缓冲区.
         *
         * @return UBuffer& 当前对象.
         */
        UBuffer &sync_from_user() {
            if (_kbuf != nullptr) {
                // 从用户空间复制数据到内核空间
                auto ret = uspace::umemcpy<uspace::CpyDir::U2K>(_tmm, _kbuf,
                                                                _uaddr, _len);
                if (!ret.has_value()) {
                    loggers::SYSCALL::ERROR("从用户空间复制数据失败: %s",
                                            to_cstring(ret.error()));
                }
            }
            return *this;
        }

        /**
         * @brief 将内核缓冲区内容复制回用户空间.
         *
         * @return Result<void> 成功返回空结果, 失败返回错误码.
         */
        [[nodiscard]]
        Result<void> commit_to_user() noexcept {
            if (_kbuf == nullptr) {
                void_return();
            }

            auto ret = uspace::umemcpy<uspace::CpyDir::K2U>(_tmm, _kbuf, _uaddr,
                                                            _len);
            if (!ret.has_value()) {
                loggers::SYSCALL::ERROR("到用户空间复制数据失败: %s",
                                        to_cstring(ret.error()));
                propagate_return(ret);
            }
            void_return();
        }

        /**
         * @brief 返回内核缓冲区地址.
         */
        [[nodiscard]] char *kbuf() const {
            return _kbuf;
        }

        /**
         * @brief 返回缓冲区长度.
         */
        [[nodiscard]] size_t len() const {
            return _len;
        }

        /**
         * @brief 返回用户空间原始地址.
         */
        [[nodiscard]] VirAddr uaddr() const {
            return _uaddr;
        }
    };

    /**
     * @brief 用户空间只读字符串代理.
     */
    class UString {
    private:
        UBuffer _ubuf;
        int _len;

    public:
        /**
         * @brief 从用户空间构造字符串代理并立即同步.
         *
         * @param uaddr 用户空间地址.
         * @param maxlen 最大读取长度.
         */
        UString(VirAddr uaddr, size_t maxlen) : _ubuf(uaddr, maxlen) {
            sync_from_user();
        }

        /**
         * @brief 复制构造字符串代理.
         *
         * @param other 被复制对象.
         */
        UString(const UString &other) : _ubuf(other.uaddr(), other.maxlen()) {
            memcpy(_ubuf.kbuf(), other.kbuf(), other.maxlen());
            _len = other._len;
        }

        /**
         * @brief 移动构造字符串代理.
         *
         * @param other 被移动对象.
         */
        UString(UString &&other) noexcept
            : _ubuf(std::move(other._ubuf)), _len(other._len) {
            other._len = 0;
        }

        /**
         * @brief 复制赋值字符串代理.
         *
         * @param other 被复制对象.
         * @return UString& 当前对象.
         */
        UString &operator=(const UString &other) {
            if (this != &other) {
                UString copied(other);
                *this = std::move(copied);
            }
            return *this;
        }

        /**
         * @brief 移动赋值字符串代理.
         *
         * @param other 被移动对象.
         * @return UString& 当前对象.
         */
        UString &operator=(UString &&other) noexcept {
            if (this != &other) {
                _ubuf     = std::move(other._ubuf);
                _len      = other._len;
                other._len = 0;
            }
            return *this;
        }

        /**
         * @brief 析构字符串代理.
         */
        ~UString() noexcept = default;

        /**
         * @brief 从用户空间重新同步字符串数据.
         *
         * @return UString& 当前对象.
         */
        UString &sync_from_user() noexcept {
            _ubuf.sync_from_user();
            _len = strnlen(kbuf(), maxlen());
            return *this;
        }

        /**
         * @brief 返回内核字符串缓冲区.
         */
        [[nodiscard]] char *kbuf() const {
            return _ubuf.kbuf();
        }

        /**
         * @brief 返回当前字符串长度.
         */
        [[nodiscard]] size_t len() const {
            return _len;
        }

        /**
         * @brief 返回最大可读长度.
         */
        [[nodiscard]] size_t maxlen() const {
            return _ubuf.len();
        }

        /**
         * @brief 返回对应用户空间地址.
         */
        [[nodiscard]] VirAddr uaddr() const {
            return _ubuf.uaddr();
        }
    };
}  // namespace syscall
