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

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "mongo/platform/basic.h"

#include "mongo/util/assert_util.h"
#include "mongo/util/decimal_counter.h"
#include "mongo/util/itoa.h"

namespace mongo {

namespace {

template <std::size_t N>
StringData toStringBuffer(std::uint64_t val, std::array<char, N>& buf) {
    struct Entry {
        char n;
        char buf[3];
    };
    static const auto table = [] {
        DecimalCounter<size_t> counter;
        std::array<Entry, 1000> v{};
        for (auto& e : v) {
            StringData src = counter;
            e.n = src.size();
            std::copy(src.begin(), src.end(), e.buf);
            ++counter;
        }
        return v;
    }();
    char* p = buf.end();
    while (val) {
        const auto& e = table[val % 1000];
        val /= 1000;
        p -= e.n;
        std::copy_n(e.buf, e.n, p);
    }
    return StringData(p, buf.end() - p);
}

}  // namespace

ItoA::ItoA(std::uint64_t val) : _str(toStringBuffer(val, _buf)) {}

}  // namespace mongo
