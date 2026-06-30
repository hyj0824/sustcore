/**
 * @file util.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief some utils
 * @version alpha-1.0.0
 * @date 2026-06-30
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

namespace utils {
    template <typename T>
    auto order_by_member(auto T::*memptr) {
        return [memptr](const T &lhs, const T &rhs) {
            return lhs.*memptr < rhs.*memptr;
        };
    }
}  // namespace utils