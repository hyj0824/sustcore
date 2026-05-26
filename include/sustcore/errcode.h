/**
 * @file errcode.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 错误码系统
 * @version alpha-1.0.0
 * @date 2026-03-03
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <nt/errors.h>
#include <sus/owner.h>

#include <expected>
#include <functional>

enum class ErrCode : int {
    GENERIC_ERROR            = 0x00'0000,
    UNKNOWN_ERROR            = 0xFF'FFFF,
    // generic errors
    SUCCESS                  = GENERIC_ERROR | 0x0000,
    FAILURE                  = GENERIC_ERROR | 0xFFFF,
    INVALID_PARAM            = GENERIC_ERROR | 0x0001,
    OUT_OF_BOUNDARY          = GENERIC_ERROR | 0x0002,
    NOT_SUPPORTED            = GENERIC_ERROR | 0x0003,
    BUSY                     = GENERIC_ERROR | 0x0004,
    OUT_OF_MEMORY            = GENERIC_ERROR | 0x0005,
    NULLPTR                  = GENERIC_ERROR | 0x0006,
    ALLOCATION_FAILED        = GENERIC_ERROR | 0x0007,
    KEY_DUPLICATED           = GENERIC_ERROR | 0x0008,
    // capability errors
    CAP_ERROR                = 0x01'0000,
    INVALID_CAPABILITY       = CAP_ERROR | 0x0001,
    INSUFFICIENT_PERMISSIONS = CAP_ERROR | 0x0003,
    TYPE_NOT_MATCHED         = CAP_ERROR | 0x0004,
    PAYLOAD_ERROR            = CAP_ERROR | 0x0005,
    CREATION_FAILED          = CAP_ERROR | 0x0006,
    INVALID_TOKEN            = CAP_ERROR | 0x0007,
    NO_FREE_SLOT             = CAP_ERROR | 0x0008,
    // fs errors
    FS_ERROR                 = 0x02'0000,
    ENTRY_NOT_FOUND          = FS_ERROR | 0x0001,
    // io errors
    IO_ERROR                 = 0x03'0000,
    // task errors
    TASK_ERROR               = 0x04'0000,
    NO_RUNNABLE_THREAD       = TASK_ERROR | 0x0001,
    NO_MESSAGE               = TASK_ERROR | 0x0002,
    // memory errors
    MEM_ERROR                = 0x05'0000,
    PAGE_NOT_PRESENT         = MEM_ERROR | 0x0001,
    INVALID_PTE              = MEM_ERROR | 0x0002,
    // device errors
    DEVICE_ERROR             = 0x06'0000,
    FDT_ERROR                = DEVICE_ERROR | 0x0001,
    // 别名
    SLOT_BUSY                = BUSY,
};

constexpr const char *to_cstring(ErrCode err) {
    switch (err) {
        case ErrCode::SUCCESS:            return "SUCCESS";
        case ErrCode::FAILURE:            return "FAILURE";
        case ErrCode::INVALID_PARAM:      return "INVALID_PARAM";
        case ErrCode::OUT_OF_BOUNDARY:    return "OUT_OF_BOUNDARY";
        case ErrCode::NOT_SUPPORTED:      return "NOT_SUPPORTED";
        case ErrCode::BUSY:               return "BUSY";
        case ErrCode::OUT_OF_MEMORY:      return "OUT_OF_MEMORY";
        case ErrCode::NULLPTR:            return "NULLPTR";
        case ErrCode::ALLOCATION_FAILED:  return "ALLOCATION_FAILED";
        case ErrCode::INVALID_CAPABILITY: return "INVALID_CAPABILITY";
        case ErrCode::INSUFFICIENT_PERMISSIONS:
            return "INSUFFICIENT_PERMISSIONS";
        case ErrCode::TYPE_NOT_MATCHED:   return "TYPE_NOT_MATCHED";
        case ErrCode::PAYLOAD_ERROR:      return "PAYLOAD_ERROR";
        case ErrCode::CREATION_FAILED:    return "CREATION_FAILED";
        case ErrCode::INVALID_TOKEN:      return "INVALID_TOKEN";
        case ErrCode::NO_FREE_SLOT:       return "NO_FREE_SLOT";
        case ErrCode::ENTRY_NOT_FOUND:    return "ENTRY_NOT_FOUND";
        case ErrCode::NO_RUNNABLE_THREAD: return "NO_RUNNABLE_THREAD";
        case ErrCode::NO_MESSAGE:         return "NO_MESSAGE";
        case ErrCode::PAGE_NOT_PRESENT:   return "PAGE_NOT_PRESENT";
        case ErrCode::INVALID_PTE:        return "INVALID_PTE";
        case ErrCode::FDT_ERROR:          return "FDT_ERROR";
        default:                          return "UNKNOWN_ERROR";
    };
}

[[deprecated(
    "该方法的返回值将从const char *变为std::string_view, "
    "请改用to_cstring()方法")]]
constexpr const char *to_string(ErrCode err) {
    return to_cstring(err);
}

template <typename T>
using Result = std::result<T, ErrCode>;
using std::unexpect;

#define unexpect_return(x)  return std::unexpected(x)
#define propagate_return(x) unexpect_return(x.error())
#define propagate(x)             \
    do {                         \
        if (!(x).has_value()) {  \
            propagate_return(x); \
        }                        \
    } while (0)
#define co_propagate(x)                             \
    do {                                            \
        if (!(x).has_value()) {                     \
            co_return std::unexpected((x).error()); \
        }                                           \
    } while (0)
#define void_return() return std::expected<void, ErrCode>{};

template <typename T>
constexpr auto always(T &&value) {
    return [value = std::forward<T>(value)](auto &&...) -> decltype(auto) {
        return value;
    };
}

template <typename T>
constexpr auto unwrap_ref() {
    return std::mem_fn(&std::reference_wrapper<T>::get);
}

template <typename T>
constexpr auto unwrap_owner() {
    return std::mem_fn(&util::owner<T>::get);
}