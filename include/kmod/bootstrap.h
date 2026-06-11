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
 * @brief 启动缓冲区中单条 bootstrap 信息的公共头部.
 */
struct BootstrapRecordHeader {
    uint32_t next;  ///< 下一条记录相对整个 bootstrap 缓冲区的偏移, 0 表示结束.
    uint32_t type;  ///< 记录类型.
};

/**
 * @brief bootstrap 记录的只读视图.
 */
struct BootstrapRecordView {
    const BootstrapRecordHeader *header;
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
    BootstrapRecordHeader header;
    CapIdx cap;

    explicit constexpr BootstrapSingleCapRecord(CapIdx value) noexcept
        : header{0, Type}, cap(value) {}
};

[[nodiscard]]
inline constexpr bool bootstrap_is_user_type(uint32_t type) noexcept {
    return (type & BOOTSTRAP_USER_TYPE_PREFIX) == BOOTSTRAP_USER_TYPE_PREFIX;
}

template <typename Callback>
[[nodiscard]]
inline bool bootstrap_foreach_record(const void *blob, size_t blob_size,
                                     Callback &&callback) {
    if (blob_size == 0) {
        return true;
    }
    if (blob == nullptr) {
        return false;
    }

    auto *bytes  = static_cast<const char *>(blob);
    size_t offset = 0;
    while (offset < blob_size) {
        if (blob_size - offset < sizeof(BootstrapRecordHeader)) {
            return false;
        }

        auto *header =
            reinterpret_cast<const BootstrapRecordHeader *>(bytes + offset);
        size_t next_offset = header->next == 0 ? blob_size : header->next;
        if (next_offset <= offset || next_offset > blob_size) {
            return false;
        }
        if (next_offset - offset < sizeof(BootstrapRecordHeader)) {
            return false;
        }

        BootstrapRecordView view{
            .header    = header,
            .data      = bytes + offset + sizeof(BootstrapRecordHeader),
            .data_size = next_offset - offset - sizeof(BootstrapRecordHeader),
        };
        callback(view);
        offset = next_offset;
    }
    return offset == blob_size;
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
inline bool bootstrap_find_single_cap(const void *blob, size_t blob_size,
                                      uint32_t type, CapIdx &cap) {
    bool found = false;
    bool ok    = bootstrap_foreach_record(
        blob, blob_size, [&](const BootstrapRecordView &view) {
            if (found || view.header->type != type) {
                return;
            }
            found = bootstrap_parse_single_cap(view, cap);
        });
    return ok && found;
}
