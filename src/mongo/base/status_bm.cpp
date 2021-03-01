/**
 *    Copyright (C) 2021-present MongoDB, Inc.
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

#include <benchmark/benchmark.h>

#include <algorithm>
#include <string>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/base/status.h"
#include "mongo/util/processinfo.h"

namespace mongo {
namespace {

/**
 * Construct and destroy OK
 */
void BM_StatusCtorDtorOK(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(Status::OK());
    }
}

BENCHMARK(BM_StatusCtorDtorOK);

/**
 * Construct and destroy
 */
void BM_StatusCtorDtor(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(
            Status(ErrorCodes::Error::InternalError,
                   "A reasonably long reason"));
    }
}

BENCHMARK(BM_StatusCtorDtor);


/**
 * Copying an uncontended Status object once.
 */
void BM_StatusRefUnref(benchmark::State& state) {
    static Status s(ErrorCodes::Error::InternalError,
                    "A reasonably long reason");
    for (auto _ : state) {
        benchmark::DoNotOptimize(Status(s));
    }
}

BENCHMARK(BM_StatusRefUnref)
        ->ThreadRange(1, 4);


/**
 * Copying a contended Status object many times.
 * Then reassigning it to OK.
 */
void BM_StatusVectorFill(benchmark::State& state) {
    size_t sz = state.range(0);
    static Status sharedStatus(ErrorCodes::Error::InternalError,
                        "A reasonably long reason");
    std::vector<Status> vec(sz, Status::OK());
    for (auto _ : state) {
        std::fill(vec.begin(), vec.end(), sharedStatus);
        std::fill(vec.begin(), vec.end(), Status::OK());
    }
}

BENCHMARK(BM_StatusVectorFill)
        ->Range(1, 64)
        ->ThreadRange(1, 4);

}  // namespace
}  // namespace mongo
