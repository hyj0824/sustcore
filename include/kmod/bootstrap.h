/**
 * @file bootstrap.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief kmod 启动缓冲区中使用的简单引导参数结构
 * @version alpha-1.0.0
 * @date 2026-06-05
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sustcore/capability.h>

/**
 * @brief 单条 bootstrap 信息的公共头部.
 */
struct bsheader {
    uint32_t size;  ///< 记录总长度, 包含头部与 payload.
    uint32_t type;  ///< 记录类型.
};

/**
 * @brief bootstrap 记录的只读视图.
 */
struct BootstrapRecordView {
    const bsheader *header;
    const void *data;
    size_t data_size;
};

/**
 * @brief FILECAPEXPLAIN/DIRCAPEXPLAIN 的解析结果.
 */
struct BootstrapCapPathView {
    CapIdx cap;
    const char *path;
};

constexpr uint32_t BOOTSTRAP_TYPE_FILECAPEXPLAIN = 0x1;
constexpr uint32_t BOOTSTRAP_TYPE_DIRCAPEXPLAIN  = 0x2;
constexpr uint32_t BOOTSTRAP_USER_TYPE_PREFIX    = 0xFFFF0000U;

/**
 * @brief 单个 CapIdx 作为 payload 的简单记录模板.
 */
template <uint32_t Type>
struct BootstrapSingleCapRecord {
    bsheader header;
    CapIdx cap;

    explicit constexpr BootstrapSingleCapRecord(CapIdx value) noexcept
        : header{sizeof(BootstrapSingleCapRecord<Type>), Type}, cap(value) {}
};

[[nodiscard]]
inline constexpr bool bootstrap_is_user_type(uint32_t type) noexcept {
    return (type & BOOTSTRAP_USER_TYPE_PREFIX) == BOOTSTRAP_USER_TYPE_PREFIX;
}

[[nodiscard]]
inline bool bootstrap_make_view(const bsheader *record,
                                BootstrapRecordView &view) noexcept {
    if (record == nullptr || record->size < sizeof(bsheader)) {
        return false;
    }
    view.header    = record;
    view.data      = reinterpret_cast<const char *>(record) + sizeof(bsheader);
    view.data_size = record->size - sizeof(bsheader);
    return true;
}

template <typename Callback>
[[nodiscard]]
inline bool bootstrap_foreach_record(const bsheader *const *bsargv,
                                     size_t bsargc, Callback &&callback) {
    if (bsargc != 0 && bsargv == nullptr) {
        return false;
    }
    for (size_t i = 0; i < bsargc; ++i) {
        BootstrapRecordView view{};
        if (!bootstrap_make_view(bsargv[i], view)) {
            return false;
        }
        callback(view);
    }
    return true;
}

[[nodiscard]]
inline bool bootstrap_parse_single_cap(const BootstrapRecordView &view,
                                       CapIdx &cap) {
    if (view.data_size != sizeof(CapIdx) || view.data == nullptr) {
        return false;
    }
    memcpy(&cap, view.data, sizeof(cap));
    return true;
}

[[nodiscard]]
inline bool bootstrap_parse_cap_path(const BootstrapRecordView &view,
                                     BootstrapCapPathView &cap_path) {
    if (view.data == nullptr || view.data_size < sizeof(CapIdx) + 1) {
        return false;
    }

    auto *bytes = static_cast<const char *>(view.data);
    memcpy(&cap_path.cap, bytes, sizeof(cap_path.cap));
    cap_path.path = bytes + sizeof(CapIdx);

    for (size_t i = sizeof(CapIdx); i < view.data_size; ++i) {
        if (bytes[i] == '\0') {
            return true;
        }
    }
    return false;
}

[[nodiscard]]
inline bool bootstrap_find_single_cap(const bsheader *const *bsargv,
                                      size_t bsargc,
                                      uint32_t type, CapIdx &cap) {
    bool found = false;
    bool ok    = bootstrap_foreach_record(
        bsargv, bsargc, [&](const BootstrapRecordView &view) {
            if (found || view.header->type != type) {
                return;
            }
            found = bootstrap_parse_single_cap(view, cap);
        });
    return ok && found;
}
