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

#include "mongo/platform/basic.h"

#include "mongo/util/itoa.h"

#include <array>
#include <cstdint>
#include <string>

#include "mongo/base/string_data.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/decimal_counter.h"

namespace mongo {
namespace {

template <std::size_t N>
constexpr std::size_t pow10() {
    std::size_t r = 1;
    for (std::size_t i = 0; i < N; ++i) {
        r *= 10;
    }
    return r;
}

constexpr std::size_t kTableDigits = 3;
constexpr std::size_t kTableSize = pow10<kTableDigits>();

auto makeTable() {
    struct Entry {
        std::uint8_t n;
        char s[kTableDigits];
    };
    std::array<Entry, kTableSize> table;
    DecimalCounter<std::size_t> i;
    for (auto& e : table) {
        StringData is = i;
        e.n = is.size();
        std::copy(is.begin(), is.end(), e.s);
        ++i;
    }
    return table;
}

}  // namespace

ItoA::ItoA(std::uint64_t val) {
    static const auto& gTable = *new auto(makeTable());
    if (val < gTable.size()) {
        const auto& e = gTable[val];
        _str = StringData(e.s, e.n);
        return;
    }
    char* p = std::end(_buf);
    while (val != 0) {
        auto r = val % kTableSize;
        val /= kTableSize;
        const auto& e = gTable[r];
        for (auto si = e.s + e.n; si != e.s;) {
            *--p = *--si;
        }
        if (val) {
            for (std::size_t i = e.n; i < kTableDigits; ++i) {
                *--p = '0';
            }
        }
    }
    _str = StringData(p, std::end(_buf) - p);
}

}  // namespace mongo
