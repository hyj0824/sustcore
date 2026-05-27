/**
 * @file guard.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief guard
 * @version alpha-1.0.0
 * @date 2026-05-20
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cap/cholder.h>
#include <sus/owner.h>
#include <sus/raii.h>

#include <cassert>
#include <utility>

template <typename T>
auto delete_guard(util::owner<T *> ptr) {
    return util::Guard([ptr = std::move(ptr)] mutable { delete ptr; });
}

inline auto remove_guard(cap::CHolder *cholder, CapIdx idx) {
    return util::Guard([cholder, idx]() {
        auto remove_res = cholder->remove(idx);
        assert(remove_res.has_value());
    });
}
