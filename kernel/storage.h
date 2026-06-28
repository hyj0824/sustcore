/**
 * @file storage.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 全局对象存储定义
 * @version alpha-1.0.0
 * @date 2026-06-28
 * 
 * @copyright Copyright (c) 2026
 * 
 */

#pragma once

#include <new>
#include <utility>

template <typename T>
class Storage {
private:
    alignas(T) char _storage[sizeof(T)];

public:
    template <typename... Args>
    T *construct(Args &&...args) {
        return std::launder(new (_storage) T(std::forward<Args>(args)...));
    }

    [[nodiscard]]
    T *get() {
        return std::launder(reinterpret_cast<T *>(_storage));
    }

    [[nodiscard]]
    const T *get() const {
        return std::launder(reinterpret_cast<const T *>(_storage));
    }

    [[nodiscard]]
    T &ref() {
        return *get();
    }

    [[nodiscard]]
    const T &ref() const {
        return *get();
    }
};
