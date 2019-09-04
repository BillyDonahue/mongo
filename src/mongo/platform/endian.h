/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>

namespace mongo::endian {

enum class Order {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X86))
    little,
    big,
    native = little,
#elif defined(__GNUC__)
    little = __ORDER_LITTLE_ENDIAN__,
    big = __ORDER_BIG_ENDIAN__,
    native = __BYTE_ORDER__,
#else
#error "Unsupported compiler or architecture"
#endif  // compiler check
};

namespace detail {

// clang-format off
template <typename T, std::size_t bits = CHAR_BIT * sizeof(T)>
using WidthRank = std::conditional_t<bits == 8, std::uint8_t,
                  std::conditional_t<bits == 16, std::uint16_t,
                  std::conditional_t<bits == 32, std::uint32_t,
                  std::conditional_t<bits == 64, std::uint64_t,
                  void>>>>;
// clang-format on

#if defined(_MSC_VER)
inline std::uint16_t bswap(std::uint16_t v) {
    return _byteswap_ushort(v);
}
inline std::uint32_t bswap(std::uint32_t v) {
    return _byteswap_ulong(v);
}
inline std::uint64_t bswap(std::uint64_t v) {
    return _byteswap_uint64(v);
}
#else   // non-MSVC
inline std::uint16_t bswap(std::uint16_t v) {
    return __builtin_bswap16(v);
}
inline std::uint32_t bswap(std::uint32_t v) {
    return __builtin_bswap32(v);
}
inline std::uint64_t bswap(std::uint64_t v) {
    return __builtin_bswap64(v);
}
#endif  // non-MSVC

template <Order From, Order To, typename T, typename Casts>
T convert(T t, Casts casts) {
    if constexpr (From == To) {
        return t;
    } else {
        static constexpr std::size_t bits = CHAR_BIT * sizeof(T);
        if constexpr (bits == 8) {
            return t;
        } else if constexpr (bits == 16) {
            return casts.out(bswap(casts.in(t)));
        } else if constexpr (bits == 32) {
            return casts.out(bswap(casts.in(t)));
        } else if constexpr (bits == 64) {
            return casts.out(bswap(casts.in(t)));
        }
    }
}

template <typename T, typename U = WidthRank<T>>
struct ImplicitCast {
    U in(T t) const {
        return t;
    }
    T out(U u) const {
        return u;
    }
};

template <typename T, typename U = WidthRank<T>>
struct BitCast {
    U in(T t) const {
        U u;
        std::memcpy(&u, &t, sizeof(t));
        return u;
    }
    T out(U u) const {
        T t;
        std::memcpy(&t, &u, sizeof(t));
        return t;
    }
};

template <typename T>
using RequiresArithmetic = std::enable_if_t<std::is_arithmetic_v<T>, int>;

template <typename T>
using Casts = std::conditional_t<std::is_floating_point_v<T>,
                                 BitCast<T>,
                                 std::conditional_t<std::is_integral_v<T>, ImplicitCast<T>, void>>;

template <typename T, RequiresArithmetic<T> = 0>
T nativeToBig(T t) {
    return convert<Order::native, Order::big>(t, Casts<T>{});
}
template <typename T, RequiresArithmetic<T> = 0>
T nativeToLittle(T t) {
    return convert<Order::native, Order::little>(t, Casts<T>{});
}
template <typename T, RequiresArithmetic<T> = 0>
T bigToNative(T t) {
    return convert<Order::big, Order::native>(t, Casts<T>{});
}
template <typename T, RequiresArithmetic<T> = 0>
T littleToNative(T t) {
    return convert<Order::little, Order::native>(t, Casts<T>{});
}

}  // namespace detail

using detail::bigToNative;
using detail::littleToNative;
using detail::nativeToBig;
using detail::nativeToLittle;

}  // namespace mongo::endian
