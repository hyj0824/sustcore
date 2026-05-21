/**
 * @file buffer.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 缓存
 * @version alpha-1.0.0
 * @date 2026-05-18
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <sus/types.h>
#include <sustcore/capability.h>
#include <sustcore/errcode.h>
#include <sustcore/msg.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace rpc {
    class ByteBuffer {
    public:
        enum direction
        {
            READ = 0,
            WRITE = 1
        };
    private:
        byte *_buf;
        size_t _size = 0;
        mutable size_t _read_pos = 0;
        size_t _capacity;
        direction _dir;

        Result<void> M_resize(size_t new_capacity) {
            if (_dir != WRITE)
                unexpect_return(ErrCode::NOT_SUPPORTED);
            byte *oldbuf = _buf;
            _capacity    = new_capacity;
            _buf         = new byte[_capacity];
            if (_buf == nullptr) {
                unexpect_return(ErrCode::ALLOCATION_FAILED);
            }
            memcpy(_buf, oldbuf, _size);
            delete[] oldbuf;
            void_return();
        }

        Result<void> M_check_size(size_t inc_sz) {
            if (_dir != WRITE)
                unexpect_return(ErrCode::NOT_SUPPORTED);
            if (_size + inc_sz > _capacity) {
                auto new_capacity = M_resize((_size + inc_sz) * 2);
                propagate(new_capacity);
            }
            void_return();
        }

    public:
        constexpr ByteBuffer(size_t capacity)
            : _buf(new byte[capacity]), _capacity(capacity), _dir(direction::WRITE) {}
        constexpr ByteBuffer(byte *buf, size_t size)
            : _buf(buf), _size(size), _capacity(size), _dir(direction::READ) {}
        ~ByteBuffer() {
            delete[] _buf;
        }
        byte *finish() {
            byte *ret = _buf;
            _buf      = nullptr;
            _capacity = _size = _read_pos = 0;
            return ret;
        }
        ByteBuffer(const ByteBuffer &buffer)
            : _buf(new byte[buffer._capacity]),
              _size(buffer._size),
              _read_pos(buffer._read_pos),
              _capacity(buffer._capacity),
              _dir(buffer._dir) {
            memcpy(_buf, buffer._buf, buffer._capacity);
        }

        ByteBuffer &operator=(const ByteBuffer &buffer) {
            if (&buffer == this)
                return *this;
            delete[] finish();
            _buf      = new byte[buffer._capacity];
            _size     = buffer._size;
            _read_pos = buffer._read_pos;
            _capacity = buffer._capacity;
            _dir      = buffer._dir;
            memcpy(_buf, buffer._buf, buffer._capacity);
            return *this;
        }

        ByteBuffer(ByteBuffer &&buffer)
            : _buf(buffer._buf),
              _size(buffer._size),
              _read_pos(buffer._read_pos),
              _capacity(buffer._capacity),
              _dir(buffer._dir) {
            buffer._buf = nullptr;
            buffer._size = buffer._read_pos = buffer._capacity = 0;
        }

        ByteBuffer &operator=(ByteBuffer &&buffer) {
            if (&buffer == this)
                return *this;
            delete[] finish();
            _buf      = buffer._buf;
            _size     = buffer._size;
            _read_pos = buffer._read_pos;
            _capacity = buffer._capacity;
            _dir      = buffer._dir;
            buffer._buf = nullptr;
            buffer._size = buffer._read_pos = buffer._capacity = 0;
            return *this;
        }

        [[nodiscard]] Result<void> writes(const byte *b, size_t sz) {
            if (_dir != WRITE)
                unexpect_return(ErrCode::NOT_SUPPORTED);

            auto check_res = M_check_size(sz);
            propagate(check_res);

            for (size_t i = 0; i < sz; i++) {
                *(_buf + _size + i) = *(b + i);
            }
            _size += sz;
            void_return();
        }

        template <typename _Tp>
        [[nodiscard]] Result<void> write(const _Tp &t) {
            return writes(reinterpret_cast<const byte *>(&t), sizeof(_Tp));
        }

        [[nodiscard]] size_t size() const {
            return _size;
        }

        [[nodiscard]] size_t capacity() const {
            return _capacity;
        }

        Result<void> reads(byte *b, size_t sz) const {
            if (_dir != READ)
                unexpect_return(ErrCode::NOT_SUPPORTED);
            if (_read_pos + sz > _size)
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            for (size_t i = 0; i < sz; i++) {
                *(b + i) = *(_buf + _read_pos + i);
            }
            _read_pos += sz;
            void_return();
        }

        template <typename _Tp>
        [[nodiscard]] Result<void> read(_Tp &t) const
        {
            return reads(reinterpret_cast<byte *>(&t), sizeof(_Tp));
        }

        template <typename _Tp>
        [[nodiscard]] Result<_Tp> read() const
        {
            if (_dir != READ)
                unexpect_return(ErrCode::NOT_SUPPORTED);
            _Tp t;
            return read(t).transform(always(t));
        }

        template <typename _Tp>
        [[nodiscard]] Result<void> peek(_Tp &t, size_t offset) const {
            if (offset > _size || sizeof(_Tp) > _size - offset)
                unexpect_return(ErrCode::OUT_OF_BOUNDARY);
            memcpy(reinterpret_cast<byte *>(&t), _buf + offset, sizeof(_Tp));
            void_return();
        }

        template <typename _Tp>
        [[nodiscard]] Result<_Tp> peek(size_t offset) const {
            _Tp t;
            return peek(t, offset).transform(always(t));
        }

        [[nodiscard]]
        constexpr byte *data() const {
            return _buf;
        }
    };
}  // namespace rpc
