/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include <array>
#include <iterator>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "mongo/client/sdam/sdam_datatypes.h"
#include "mongo/client/sdam/server_description.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/optional_util.h"

/**
 * The following facilitates writing tests in the Server Discovery And Monitoring (sdam) namespace.
 */
namespace mongo::sdam {

namespace test_stream_extension {

template <typename T> inline constexpr bool isStdVector = false;
template <typename... Ts> inline constexpr bool isStdVector<std::vector<Ts...>> = true;

template <typename T> inline constexpr bool isStdSet = false;
template <typename... Ts> inline constexpr bool isStdSet<std::set<Ts...>> = true;

template <typename T> inline constexpr bool isStdMap = false;
template <typename... Ts> inline constexpr bool isStdMap<std::map<Ts...>> = true;

template <typename T> inline constexpr bool isStdPair = false;
template <typename... Ts> inline constexpr bool isStdPair<std::pair<Ts...>> = true;

template <typename T>
std::ostream& stream(std::ostream& os, const T& v);

template <typename Seq>
std::ostream& streamSequence(std::ostream& os, std::array<StringData, 2> braces, const Seq& seq) {
    bool sep = false;
    os << braces[0];
    for (const auto& item : seq) {
        if (sep)
            os << ", ";
        sep = true;
        stream(os, item);
    }
    os << braces[1];
    return os;
}

template <typename T>
struct Extension {
    Extension(const T& v) : v{v} {}

    const T& operator*() const {
        return v;
    }

    template <typename U>
    friend bool operator==(const Extension& a, const Extension<U>& b) {
        return *a == *b;
    }
    template <typename U>
    friend bool operator!=(const Extension& a, const Extension<U>& b) {
        return *a != *b;
    }
    template <typename U>
    friend bool operator<(const Extension& a, const Extension<U>& b) {
        return *a < *b;
    }
    template <typename U>
    friend bool operator>(const Extension& a, const Extension<U>& b) {
        return *a > *b;
    }
    template <typename U>
    friend bool operator<=(const Extension& a, const Extension<U>& b) {
        return *a <= *b;
    }
    template <typename U>
    friend bool operator>=(const Extension& a, const Extension<U>& b) {
        return *a >= *b;
    }

    friend std::ostream& operator<<(std::ostream& os, const Extension& ext) {
        return stream(os, *ext);
    }

    const T& v;
};

template <typename T>
std::ostream& stream(std::ostream& os, const T& v) {
    if constexpr (isStdVector<T>) {
        return streamSequence(os, {"[", "]"}, v);
    } else if constexpr (isStdSet<T>) {
        return streamSequence(os, {"{", "}"}, v);
    } else if constexpr (isStdMap<T>) {
        return streamSequence(os, {"{", "}"}, v);
    } else if constexpr (isStdPair<T>) {
        return os << Extension{v.first} << ": " << Extension{v.second};
    } else if constexpr (std::is_same_v<T, std::nullopt_t>) {
        return os << "--";
    } else if constexpr (isStdOptional<T>) {
        if (!v)
            return os << Extension{std::nullopt};
        return os << " " << Extension{*v};
    } else {
        return os << v;
    }
}

}  // namespace test_stream_extension

/**
 * Facade for use in ASSERTions. Presents pass-through relational ops and custom streaming
 * behavior around arbitrary object `v`.
 */
template <typename T>
auto adaptForAssert(const T& v) {
    return test_stream_extension::Extension{v};
}

class SdamTestFixture : public unittest::Test {
protected:
    template <typename T, typename F, typename U = std::invoke_result_t<F, T>>
    std::vector<U> map(const std::vector<T>& source, const F& f) {
        std::vector<U> result;
        std::transform(source.begin(), source.end(), std::back_inserter(result), f);
        return result;
    }

    template <typename T, typename F, typename U = std::invoke_result_t<F, T>>
    std::set<U> mapSet(const std::vector<T>& source, const F& f) {
        auto v = map(source, f);
        return std::set<U>(v.begin(), v.end());
    }
};

}  // namespace mongo::sdam
