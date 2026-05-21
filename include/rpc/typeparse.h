/**
 * @file typeparse.h
 * @author theflysong (song_of_the_fly@163.com)
 * @brief 解析类型数据
 * @version alpha-1.0.0
 * @date 2026-05-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <rpc/packet.h>
#include <sus/reflection.h>
#include <sus/types.h>

#include <concepts>
#include <meta>
#include <type_traits>

namespace rpc {
    template <typename T>
    concept primitive_type = std::same_as<std::remove_cvref_t<T>, sus_u8> ||
                             std::same_as<std::remove_cvref_t<T>, sus_u16> ||
                             std::same_as<std::remove_cvref_t<T>, sus_u32> ||
                             std::same_as<std::remove_cvref_t<T>, sus_u64> ||
                             std::same_as<std::remove_cvref_t<T>, sus_i8> ||
                             std::same_as<std::remove_cvref_t<T>, sus_i16> ||
                             std::same_as<std::remove_cvref_t<T>, sus_i32> ||
                             std::same_as<std::remove_cvref_t<T>, sus_i64> ||
                             std::same_as<std::remove_cvref_t<T>, float> ||
                             std::same_as<std::remove_cvref_t<T>, double> ||
                             std::same_as<std::remove_cvref_t<T>, bool> ||
                             std::same_as<std::remove_cvref_t<T>, void>;

    template <typename T>
    inline constexpr bool dependent_false = false;

    template <typename T>
        requires primitive_type<T>
    constexpr PrimitiveTypeId get_primitive_type_id() {
        using Type = std::remove_cvref_t<T>;
        if constexpr (std::same_as<Type, sus_u8>) {
            return PrimitiveTypeId::u8;
        } else if constexpr (std::same_as<Type, sus_u16>) {
            return PrimitiveTypeId::u16;
        } else if constexpr (std::same_as<Type, sus_u32>) {
            return PrimitiveTypeId::u32;
        } else if constexpr (std::same_as<Type, sus_u64>) {
            return PrimitiveTypeId::u64;
        } else if constexpr (std::same_as<Type, sus_i8>) {
            return PrimitiveTypeId::i8;
        } else if constexpr (std::same_as<Type, sus_i16>) {
            return PrimitiveTypeId::i16;
        } else if constexpr (std::same_as<Type, sus_i32>) {
            return PrimitiveTypeId::i32;
        } else if constexpr (std::same_as<Type, sus_i64>) {
            return PrimitiveTypeId::i64;
        } else if constexpr (std::same_as<Type, float>) {
            return PrimitiveTypeId::f32;
        } else if constexpr (std::same_as<Type, double>) {
            return PrimitiveTypeId::f64;
        } else if constexpr (std::same_as<Type, bool>) {
            return PrimitiveTypeId::boolean;
        } else if constexpr (std::same_as<Type, void>) {
            return PrimitiveTypeId::void_type;
        } else {
            static_assert(dependent_false<T>, "Unreachable control flow!");
        }
    }

    struct custom_type {
        sus_u32 type_id;
        constexpr custom_type(sus_u32 type_id) : type_id(type_id) {}
    };

    template <typename T>
    consteval sus_u32 get_type_id() {
        using Type = std::remove_cvref_t<T>;
        if constexpr (primitive_type<Type>) {
            return static_cast<sus_u32>(get_primitive_type_id<Type>());
        } else if constexpr (util::is_annotated_by<Type, custom_type>) {
            return util::get_annotation<Type, custom_type>().type_id;
        } else {
            static_assert(dependent_false<Type>,
                          "Invalid type! use custom_type to annotated "
                          "server-defined types!");
        }
    }

    template <typename T>
    constexpr bool do_type_match(sus_u32 type_id) {
        return get_type_id<T>() == type_id;
    }

    template <typename FuncType>
    struct argument_match_traits;

    template <typename R, typename... Args>
    struct argument_match_traits<R(Args...)> {
        static constexpr bool match(const std::vector<sus_u32> &typelst) {
            if (sizeof...(Args) != typelst.size()) {
                return false;
            }

            if constexpr (sizeof...(Args) == 0) {
                return true;
            } else {
                size_t cnt = 0;
                return ((do_type_match<Args>(typelst[cnt++])) && ...);
            }
        }
    };

    template <typename R, typename... Args>
    struct argument_match_traits<R(Args...) const> : argument_match_traits<R(Args...)> {};

    template <typename FuncType>
    constexpr bool do_arguments_match(const std::vector<sus_u32> &typelst) {
        return argument_match_traits<FuncType>::match(typelst);
    }
}  // namespace rpc
