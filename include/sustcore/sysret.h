/**
 * @file sysret.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 系统调用返回
 * @version alpha-1.0.0
 * @date 2026-06-26
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <std/c++/features/attributes.h>
#include <sustcore/errcode.h>

template <typename T>
struct SysRet {
    size_t ret0;
    size_t ret1;

    __ATTR_ALWAYS_INLINE__ [[nodiscard]]
    T value() const {
        return static_cast<T>(ret0);
    }

    __ATTR_ALWAYS_INLINE__ [[nodiscard]]
    ErrCode error() const {
        return static_cast<ErrCode>(ret1);
    }

    __ATTR_ALWAYS_INLINE__ [[nodiscard]]
    bool is_error() const {
        return error() != ErrCode::SUCCESS;
    }

    __ATTR_ALWAYS_INLINE__ [[nodiscard]]
    Result<T> to_result() const {
        if (is_error()) {
            return std::unexpected(error());
        }
        return value();
    }
};

template <>
struct SysRet<void> {
    size_t ret0;
    size_t ret1;

    __ATTR_ALWAYS_INLINE__ [[nodiscard]]
    ErrCode error() const {
        return static_cast<ErrCode>(ret1);
    }

    __ATTR_ALWAYS_INLINE__ [[nodiscard]]
    bool is_error() const {
        return error() != ErrCode::SUCCESS;
    }

    __ATTR_ALWAYS_INLINE__
    operator bool() const {
        return !is_error();
    }

    __ATTR_ALWAYS_INLINE__ [[nodiscard]]
    Result<void> to_result() const {
        if (is_error()) {
            return std::unexpected(error());
        }
        void_return();
    }
};
