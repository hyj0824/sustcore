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

#include <cstddef>
#include <cstring>

namespace syscall {
    // 将用户空间中的数据读取到内核空间中
    class UBuffer {
    private:
        VirAddr _uaddr;             // 用户空间地址
        util::owner<char *> _kbuf;  // 内核空间缓冲区地址
        size_t _len;                // 数据长度
        TaskMemoryManager *_tmm;    // 用户空间内存管理器
    public:
        UBuffer(VirAddr uaddr, size_t len)
            : _uaddr(uaddr), _len(len), _tmm(env::inst().tmm()) {
            // 分配内核空间缓冲区
            _kbuf = util::owner(new char[_len]);
        }

        ~UBuffer() {
            loggers::SYSCALL::DEBUG("释放内核缓冲区: %p", _kbuf.get());
            delete[] _kbuf;
        }

        UBuffer &sync_from_user() {
            // 从用户空间复制数据到内核空间
            auto ret =
                uspace::umemcpy<uspace::CpyDir::U2K>(_tmm, _kbuf, _uaddr, _len);
            if (!ret.has_value()) {
                loggers::SYSCALL::ERROR("从用户空间复制数据失败: %s",
                                        to_cstring(ret.error()));
            }
            return *this;
        }

        UBuffer &sync_to_user() {
            // 从内核空间复制数据到用户空间
            auto ret =
                uspace::umemcpy<uspace::CpyDir::K2U>(_tmm, _kbuf, _uaddr, _len);
            if (!ret.has_value()) {
                loggers::SYSCALL::ERROR("到用户空间复制数据失败: %s",
                                        to_cstring(ret.error()));
            }
            return *this;
        }

        [[nodiscard]] char *kbuf() const {
            return _kbuf;
        }

        [[nodiscard]] size_t len() const {
            return _len;
        }

        [[nodiscard]] VirAddr uaddr() const {
            return _uaddr;
        }
    };

    // 用户空间字符串(readonly)
    class UString {
    private:
        UBuffer _ubuf;
        int _len;

    public:
        UString(VirAddr uaddr, size_t maxlen) : _ubuf(uaddr, maxlen) {
            sync_from_user();
        }

        ~UString() {}

        UString &sync_from_user() {
            _ubuf.sync_from_user();
            _len = strnlen(kbuf(), maxlen());
            return *this;
        }

        [[nodiscard]] char *kbuf() const {
            return _ubuf.kbuf();
        }

        [[nodiscard]] size_t len() const {
            return _len;
        }

        [[nodiscard]] size_t maxlen() const {
            return _ubuf.len();
        }

        [[nodiscard]] VirAddr uaddr() const {
            return _ubuf.uaddr();
        }
    };
}  // namespace syscall
