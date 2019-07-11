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
    constexpr int kTableDigits = 4;
    constexpr std::size_t kTableSize = pow10<kTableDigits>();
    struct Entry {
        std::uint8_t n;
        char s[kTableDigits];
    };
    std::array<Entry, kTableSize> table;

    int nd = 1;
    auto e = table.begin();
    std::array<char, kTableDigits> d;
    d[0] = '0'; for (int i = 10; i--; ++d[0], nd = std::max(nd, kTableDigits - 0)) {
    d[1] = '0'; for (int i = 10; i--; ++d[1], nd = std::max(nd, kTableDigits - 1)) {
    d[2] = '0'; for (int i = 10; i--; ++d[2], nd = std::max(nd, kTableDigits - 2)) {
    d[3] = '0'; for (int i = 10; i--; ++d[3], nd = std::max(nd, kTableDigits - 3)) {
        std::copy(d.begin(), d.end(), e->s);
        e->n = nd;
        ++e;
    }}}}
    return table;
}

namespace const_experiment {

constexpr int kTableDigits = 4;
constexpr std::size_t kTableSize = pow10<kTableDigits>();

struct Entry {
    std::uint8_t n;
    char s[kTableDigits];
};

constexpr Entry makeEntry(std::size_t i) {
    constexpr const char d[] = "0123456789";
    return {
        static_cast<std::uint8_t>(
            i/pow10<3>() ? 4 :
            i/pow10<2>() ? 3 :
            i/pow10<1>() ? 2 :
            1),
        {
            d[(i/1000)%10],
            d[(i/100)%10],
            d[(i/10)%10],
            d[i%10],
        }
    };
}

template <std::size_t N, std::size_t... Is>
constexpr std::array<Entry, N> makeTableFold(std::index_sequence<Is...>) {
    return { makeEntry(Is)..., };
}

constexpr auto makeTableConst() {
    return makeTableFold<kTableSize>(std::make_index_sequence<kTableSize>{});
}

} // namespace const_experiment

void BM_makeTableExp(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(makeTableExp());
    }
}

void BM_makeTableConst(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(const_experiment::makeTableConst());
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
BENCHMARK(BM_makeTableConst);
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
