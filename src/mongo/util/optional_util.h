/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

/**
 * Helpers for use with boost::optional or std::optional
 */

#include <boost/optional.hpp>
#include <iostream>
#include "mongo/stdx/type_traits.h"

namespace mongo {

namespace optional_detail {

template <typename Stream, typename T>
using CanNativelyStreamOp = decltype(std::declval<Stream&>() << std::declval<const T&>());
template <typename Stream, typename T>
inline constexpr bool canNativelyStream = stdx::is_detected_v<CanNativelyStreamOp, Stream, T>;

template <typename T>
inline constexpr bool isStdOptional = false;
template <typename T>
inline constexpr bool isStdOptional<std::optional<T>> = true;

template <typename T>
inline constexpr bool isBoostOptional = false;
template <typename T>
inline constexpr bool isBoostOptional<boost::optional<T>> = true;

template <typename T>
class StreamPut {
public:
    explicit StreamPut(const T& v) : _v{v} {}

private:
    template <typename Stream>
    friend Stream& operator<<(Stream& os, const StreamPut& put) {
        if constexpr (std::is_same_v<T, std::nullopt_t> ||
                      std::is_same_v<T, boost::none_t>)
            return os << "--";
        else if constexpr (isStdOptional<T> || isBoostOptional<T>) {
            if (!put._v)
                return os << StreamPut{std::nullopt};
            return os << " " << StreamPut{*put._v};
        } else {
            return os << put._v;
        }
    }
    const T& _v;
};

}  // namespace optional_detail

template <typename Stream>
Stream& toStream(Stream& os, boost::none_t) {
    return os << optional_detail::StreamPut{boost::none};
}

template <typename Stream, typename T, std::enable_if_t<optional_detail::canNativelyStream<Stream, T>, int> = 0>
Stream& toStream(Stream& os, const boost::optional<T>& v) {
    os << optional_detail::StreamPut{v};
    return os;
}

template <typename Stream>
Stream& toStream(Stream& os, std::nullopt_t) {
    os << optional_detail::StreamPut{std::nullopt};
    return os;
}

template <typename Stream, typename T, std::enable_if_t<optional_detail::canNativelyStream<Stream, T>, int> = 0>
Stream& toStream(Stream& os, const std::optional<T>& v) {
    os << optional_detail::StreamPut{v};
    return os;
}

}  // namespace mongo
