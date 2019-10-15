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


#include "mongo/platform/basic.h"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>

#include <benchmark/benchmark.h>

#include "mongo/base/string_data.h"
#include "mongo/util/decimal_counter.h"
#include "mongo/util/stacktrace.h"

namespace mongo {
namespace {

struct RecursionParam {
    std::function<void()> f;
    std::vector<std::function<void(RecursionParam&)>> stack;
};

// Pops a callable and calls it. printStackTrace when we're out of callables.
// Calls itself a few times to synthesize a nice big call stack.
MONGO_COMPILER_NOINLINE int recursionTest(RecursionParam& p) {
    if (p.stack.empty()) {
        // I've come to invoke `stack` elements and test `printStackTrace()`,
        // and I'm all out of `stack` elements.
        p.f();
        return 0;
    }
    auto f = std::move(p.stack.back());
    p.stack.pop_back();
    f(p);
    return 0;
}

void BM_Incr(benchmark::State& state) {
    size_t items = 0;
    RecursionParam param;
    size_t i = 0;
    param.f = [&] { ++i; };
    for (auto _ : state) {
        benchmark::DoNotOptimize(recursionTest(param));
        ++items;
    }
    state.SetItemsProcessed(items);
}
BENCHMARK(BM_Incr)->Range(1, 100);

void BM_Backtrace(benchmark::State& state) {
    size_t items = 0;
    RecursionParam param;
    void* p[100];
    param.f = [&] {
        stack_trace::BacktraceOptions btOpt{};
        backtrace(btOpt, p, 100);
    };
    for (auto _ : state) {
        benchmark::DoNotOptimize(recursionTest(param));
        ++items;
    }
    state.SetItemsProcessed(items);
}
BENCHMARK(BM_Backtrace)->Range(1, 100);

void BM_Print(benchmark::State& state) {
    size_t items = 0;
    RecursionParam param;
    std::ostringstream os;
    param.f = [&] { os.clear(); printStackTrace(os); };
    for (auto _ : state) {
        benchmark::DoNotOptimize(recursionTest(param));
        ++items;
    }
    state.SetItemsProcessed(items);
}
BENCHMARK(BM_Print)->Range(1, 100);

}  // namespace
}  // namespace mongo
