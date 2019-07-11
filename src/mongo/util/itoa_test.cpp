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

#include <array>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/decimal_counter.h"
#include "mongo/util/itoa.h"

namespace mongo {
namespace {

TEST(ItoA, StringDataEquality) {
    std::vector<uint64_t> cases;
    auto caseInsert = std::back_inserter(cases);

    // Interesting values.
    for (auto i : std::vector<uint64_t>{0,
                                        1,
                                        9,
                                        10,
                                        11,
                                        12,
                                        99,
                                        100,
                                        101,
                                        110,
                                        133,
                                        1446,
                                        17789,
                                        192923,
                                        2389489,
                                        29313479,
                                        1928127389,
                                        std::numeric_limits<uint64_t>::max() - 1,
                                        std::numeric_limits<uint64_t>::max()}) {
        *caseInsert++ = i;
    }

    // Ramp of first several thousand values.
    for (uint64_t i = 0; i < 100'000; ++i) {
        *caseInsert++ = i;
    }

    // Large # of pseudorandom integers.
    std::mt19937 gen(0);  // deterministic seed 0
    std::uniform_int_distribution<uint64_t> dis;
    for (uint64_t i = 0; i < 1'000'000; ++i) {
        *caseInsert++ = dis(gen);
    }

    for (const auto& i : cases) {
        ASSERT_EQ(StringData(ItoA{i}), std::to_string(i));
    }
}

}  // namespace
}  // namespace mongo
