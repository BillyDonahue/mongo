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
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "mongo/base/string_data.h"
#include "mongo/util/decimal_counter.h"
#include "mongo/util/itoa.h"

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

template <std::size_t N, typename T, typename ToStr>
auto makeTable(T i, ToStr toStr) {
    constexpr std::size_t kTableDigits = N;
    constexpr std::size_t kTableSize = pow10<kTableDigits>();
    struct Entry {
        std::uint8_t n;
        char s[kTableDigits];
    };
    std::array<Entry, kTableSize> table;
    for (auto& e : table) {
        auto is = toStr(i);
        e.n = is.size();
        std::copy(is.begin(), is.end(), e.s);
        ++i;
    }
    return table;
}

template <std::size_t N>
void BM_makeTableOld(benchmark::State& state) {
    for (auto _ : state) {
        makeTable<N>(std::size_t{0}, [](auto&& i) { return std::to_string(i); });
    }
}

template <std::size_t N>
void BM_makeTableNew(benchmark::State& state) {
    for (auto _ : state) {
        makeTable<N>(DecimalCounter<std::size_t>(), [](auto&& i) { return StringData(i); });
    }
}

auto makeTableExp() {
    constexpr std::size_t kTableDigits = 4;
    constexpr std::size_t kTableSize = pow10<kTableDigits>();
    struct Entry {
        std::uint8_t n;
        char s[kTableDigits];
    };
    std::array<Entry, kTableSize> table;
    int nd = 1;
    auto e = table.begin();
    for (int d0 = 0; d0 < 10; ++d0) {
        for (int d1 = 0; d1 < 10; ++d1) {
            for (int d2 = 0; d2 < 10; ++d2) {
                for (int d3 = 0; d3 < 10; ++d3) {
                    e->n = nd;
                    e->s[0] = '0' + d3;
                    e->s[1] = '0' + d2;
                    e->s[2] = '0' + d1;
                    e->s[3] = '0' + d0;
                    ++e;
                }
                nd = std::max(nd, 2);
            }
            nd = std::max(nd, 3);
        }
        nd = std::max(nd, 4);
    }
    return table;
}

void BM_makeTableExp(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(makeTableExp());
    }
}

void BM_ItoA(benchmark::State& state) {
    std::uint64_t n = state.range(0);
    std::uint64_t items = 0;
    for (auto _ : state) {
        for (std::uint64_t i = 0; i < n; ++i) {
            benchmark::DoNotOptimize(ItoA(i));
            ++items;
        }
    }
    state.SetItemsProcessed(items);
}

void BM_ItoADigits(benchmark::State& state) {
    std::uint64_t n = state.range(0);
    std::uint64_t items = 0;

    std::uint64_t v = 0;
    for (std::uint64_t i = 0; i < n; ++i) {
        v = v * 10 + 9;
    }

    for (auto _ : state) {
        for (std::uint64_t i = 0; i < n; ++i) {
            benchmark::DoNotOptimize(ItoA(v));
            ++items;
        }
    }
    state.SetItemsProcessed(items);
}

BENCHMARK_TEMPLATE(BM_makeTableOld, 3);
BENCHMARK_TEMPLATE(BM_makeTableOld, 4);
BENCHMARK_TEMPLATE(BM_makeTableNew, 3);
BENCHMARK_TEMPLATE(BM_makeTableNew, 4);
BENCHMARK(BM_makeTableExp);
BENCHMARK(BM_ItoA)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Arg(10000000);
BENCHMARK(BM_ItoADigits)->DenseRange(1, 20);

}  // namespace
}  // namespace mongo
