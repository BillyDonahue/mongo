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

#include <iomanip>

#include <fmt/format.h>

#include "mongo/stdx/chrono.h"
#include "mongo/stdx/type_traits.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/duration.h"

namespace mongo {
namespace {

using namespace fmt::literals;

#define DURATION_TEST_ASSERT_OVERFLOW(...) \
    ASSERT_THROWS_CODE(__VA_ARGS__, AssertionException, ErrorCodes::DurationOverflow)

TEST(Duration, Assignment) {
    Milliseconds ms = Milliseconds{15};
    Milliseconds ms2 = ms;
    Milliseconds ms3 = Milliseconds{30};
    ms3 = ms;
    ASSERT_EQ(ms, ms3);
    ASSERT_EQ(ms2, ms3);
}

// The DurationTestSameType Compare* tests server to check the implementation of the comparison
// operators as well as the compare() method, and so sometimes must explicitly check ASSERT_FALSE(v1
// OP v2). The DurationTestDifferentTypes Compare* tests rely on the fact that the operators all
// just call compare(), so if the operators worked in the SameType tests, they can be trusted in the
// DifferentType tests. As such, the DifferentType tests only use ASSERT_{GT,LT,LTE,GTE,EQ,NE} and
// never do an ASSERT_FALSE.

TEST(Duration, EqualHomogeneous) {
    ASSERT_EQ(Microseconds::zero(), Microseconds::zero());
    ASSERT_EQ(Microseconds::max(), Microseconds::max());
    ASSERT_EQ(Microseconds::min(), Microseconds::min());
    ASSERT_FALSE(Microseconds::zero() == Microseconds{-1});
}

TEST(Duration, NotEqualHomogeneous) {
    ASSERT_NE(Microseconds{1}, Microseconds::zero());
    ASSERT_NE(Microseconds{-1}, Microseconds{1});
    ASSERT_FALSE(Microseconds::zero() != Microseconds{0});
}

TEST(Duration, GreaterHomogeneous) {
    ASSERT_GT(Microseconds::zero(), Microseconds::min());
    ASSERT_GT(Microseconds{Microseconds::min().count() + 1}, Microseconds::min());
    ASSERT_FALSE(Microseconds{-10} > Microseconds{103});
    ASSERT_FALSE(Microseconds{1} > Microseconds{1});
}

TEST(Duration, LessHomogeneous) {
    ASSERT_LT(Microseconds::zero(), Microseconds::max());
    ASSERT_LT(Microseconds{Microseconds::max().count() - 1}, Microseconds::max());
    ASSERT_LT(Microseconds{1}, Microseconds{10});
    ASSERT_FALSE(Microseconds{1} < Microseconds{1});
    ASSERT_FALSE(Microseconds{-3} < Microseconds{-1200});
}

TEST(Duration, GreaterEqualHomogeneous) {
    ASSERT_GTE(Microseconds::zero(), Microseconds::min());
    ASSERT_GTE(Microseconds{Microseconds::min().count() + 1}, Microseconds::min());
    ASSERT_GTE(Microseconds::max(), Microseconds::max());
    ASSERT_GTE(Microseconds::min(), Microseconds::min());
    ASSERT_GTE(Microseconds{5}, Microseconds{5});
    ASSERT_FALSE(Microseconds{-10} > Microseconds{103});
}

TEST(Duration, LessEqualHomogeneous) {
    ASSERT_LTE(Microseconds::zero(), Microseconds::max());
    ASSERT_LTE(Microseconds{Microseconds::max().count() - 1}, Microseconds::max());
    ASSERT_LTE(Microseconds{1}, Microseconds{10});
    ASSERT_LTE(Microseconds{1}, Microseconds{1});
    ASSERT_FALSE(Microseconds{-3} < Microseconds{-1200});
}

// Since comparison operators are implemented in terms of Duration::compare, we do not need to
// re-test all of the operators when the duration types are different. It suffices to know that
// compare works, which can be accomplished with EQ, NE and LT alone.

TEST(Duration, EqualHeterogeneous) {
    ASSERT_EQ(Seconds::zero(), Milliseconds::zero());
    ASSERT_EQ(Seconds{16}, Milliseconds{16000});
    ASSERT_EQ(Minutes{60}, Hours{1});
}

TEST(Duration, NotEqualHeterogeneous) {
    ASSERT_NE(Milliseconds::max(), Seconds::max());
    ASSERT_NE(Milliseconds::min(), Seconds::min());
    ASSERT_NE(Seconds::max(), Milliseconds::max());
    ASSERT_NE(Seconds::min(), Milliseconds::min());
    ASSERT_NE(Seconds{1}, Milliseconds{1});
}

TEST(Duration, LessHeterogeneous) {
    ASSERT_LT(Milliseconds{1}, Seconds{1});
    ASSERT_LT(Milliseconds{999}, Seconds{1});
    ASSERT_LT(Seconds{1}, Milliseconds{1001});
    ASSERT_LT(Milliseconds{-1001}, Seconds{-1});
    ASSERT_LT(Seconds{-1}, Milliseconds{-1});
    ASSERT_LT(Seconds{-1}, Milliseconds{-999});
}

TEST(Duration, ExtremeValuesHeterogeneous) {
    ASSERT_LT(Milliseconds::max(), Seconds::max());
    ASSERT_LT(Seconds::min(), Milliseconds::min());
    ASSERT_LT(Milliseconds::min(),
              duration_cast<Milliseconds>(duration_cast<Seconds>(Milliseconds::min())));
    ASSERT_GT(Milliseconds::max(),
              duration_cast<Milliseconds>(duration_cast<Seconds>(Milliseconds::max())));
}

TEST(Duration, ConstexprExtremeValuesHeterogeneous) {
    static_assert(Milliseconds::max() < Seconds::max());
    static_assert(Seconds::min() < Milliseconds::min());
    static_assert(Milliseconds::min() <
                  duration_cast<Milliseconds>(duration_cast<Seconds>(Milliseconds::min())));
    static_assert(Milliseconds::max() >
                  duration_cast<Milliseconds>(duration_cast<Seconds>(Milliseconds::max())));
}

TEST(Duration, Add) {
    ASSERT_EQ(Milliseconds{1001}, Milliseconds{1} + Seconds{1});
    ASSERT_EQ(Milliseconds{1001}, Seconds{1} + Milliseconds{1});
    ASSERT_EQ(Milliseconds{1001}, Milliseconds{1} + Milliseconds{1000});

    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds::max() + Milliseconds{1}) << "Max + 1 throws";
    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds::min() + Milliseconds{-1}) << "Min + -1 throws";
    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds{Seconds::min()})
        << "Seconds::min() to Milliseconds throws";
    DURATION_TEST_ASSERT_OVERFLOW(Seconds::min() + Milliseconds{1})
        << "Seconds::min() to Milliseconds throws";
    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds{1} + Seconds::min())
        << "Seconds::min() to Milliseconds throws";
}

TEST(Duration, Subtract) {
    ASSERT_EQ(Milliseconds{-999}, Milliseconds{1} - Seconds{1});
    ASSERT_EQ(Milliseconds{999}, Seconds{1} - Milliseconds{1});
    ASSERT_EQ(Milliseconds{-999}, Milliseconds{1} - Milliseconds{1000});
    ASSERT_EQ(Milliseconds::zero() - Milliseconds{1}, -Milliseconds{1});

    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds::min() - Milliseconds{1}) << "Min-1 throws";
    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds::max() - Milliseconds{-1}) << "Max-(-1) throws";
    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds{Seconds::min()})
        << "Seconds::min() to Milliseconds throws";
    DURATION_TEST_ASSERT_OVERFLOW(Seconds::min() - Milliseconds{1})
        << "Seconds::min() to Milliseconds throws";
    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds{1} - Seconds::min())
        << "Seconds::min() to Milliseconds throws";
}

TEST(Duration, ScalarMultiply) {
    ASSERT_EQ(Milliseconds{150}, 15 * Milliseconds{10});
    ASSERT_EQ(Milliseconds{150}, Milliseconds{15} * 10);

    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds::max() * 2);
    DURATION_TEST_ASSERT_OVERFLOW(2 * Milliseconds::max());
    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds::max() * -2);
    DURATION_TEST_ASSERT_OVERFLOW(-2 * Milliseconds::max());
}

TEST(Duration, ScalarDivide) {
    ASSERT_EQ(Milliseconds{-1}, Milliseconds{2} / -2);

    DURATION_TEST_ASSERT_OVERFLOW(Milliseconds::min() / -1);
}

class DurationCast : public unittest::Test {
public:
    using Rep = typename Milliseconds::rep;
    using URep = std::make_unsigned_t<Rep>;

    static inline auto ident = [](auto x) { return x; };
    static inline auto toDouble = [](auto x) { return static_cast<double>(x); };
    static inline auto toURep = [](auto x) { return static_cast<URep>(x); };

    static inline auto xform = [](auto inputStage, auto toResult) {
        return [=](auto x) { return toResult(inputStage(x)); };
    };

    static inline auto validate = [](auto val, auto transform, auto makeExpected, StringData msg) {
        auto expected = makeExpected(val);
        auto result = transform(val);
        ASSERT_EQ(result, expected) << fmt::format("scenario: {{val:{}, msg:\"{}\"}}", val, msg);
    };

    template <typename Dur>
    struct CoerceFunction {
        template <typename T>
        auto operator()(T&& x) const {
            using Per = typename Dur::period;
            return duration_cast<Milliseconds>(deduceChronoDuration<Per>(x));
        }
    };
};

TEST_F(DurationCast, NonTruncatingDurationCasts) {
    ASSERT_EQ(1, duration_cast<Seconds>(Milliseconds{1000}).count());
    ASSERT_EQ(1000, duration_cast<Milliseconds>(Seconds{1}).count());
    ASSERT_EQ(1000, Milliseconds{Seconds{1}}.count());
    ASSERT_EQ(1053, duration_cast<Milliseconds>(Milliseconds{1053}).count());
}

TEST_F(DurationCast, TruncatingDurationCasts) {
    ASSERT_EQ(1, duration_cast<Seconds>(Milliseconds{1600}).count());
    ASSERT_EQ(0, duration_cast<Seconds>(Milliseconds{999}).count());
    ASSERT_EQ(-1, duration_cast<Seconds>(Milliseconds{-1600}).count());
    ASSERT_EQ(0, duration_cast<Seconds>(Milliseconds{-999}).count());
}

TEST_F(DurationCast, OverflowingCastsThrow) {
    ASSERT_THROWS_CODE(duration_cast<Milliseconds>(Seconds::max()),
                       AssertionException,
                       ErrorCodes::DurationOverflow);
    ASSERT_THROWS_CODE(duration_cast<Milliseconds>(Seconds::min()),
                       AssertionException,
                       ErrorCodes::DurationOverflow);
}

/** Converting from one Duration period to another. */
TEST_F(DurationCast, CastFromHeterogeneous) {
    constexpr auto ms = duration_cast<Milliseconds>(Seconds(2));
    ASSERT_EQ(2000, ms.count());
    constexpr auto secs = duration_cast<Seconds>(ms);
    ASSERT_EQ(2, secs.count());
}

/** Converting from std::chrono::duration to Duration. */
TEST_F(DurationCast, ConstructorFromChronoDuration) {
    constexpr auto ms = duration_cast<Milliseconds>(stdx::chrono::seconds(2));
    ASSERT_EQ(2000, ms.count());
}

/** Test implicit conversion constructor. */
TEST_F(DurationCast, ConstructorFromHeterogeneous) {
    constexpr Milliseconds ms = Seconds(2);
    ASSERT_EQ(2000, ms.count());
}

TEST_F(DurationCast, CastFromSystemDuration) {
    auto standardMillis = Milliseconds{10}.toSystemDuration();
    ASSERT_EQUALS(duration_cast<Milliseconds>(standardMillis), Milliseconds{10});
}

TEST_F(DurationCast, SamePrecisionCoerce) {
    auto makeExpected = [](Rep x) { return Milliseconds(x); };
    auto toResult = CoerceFunction<Milliseconds>{};

    validate(1000, xform(ident, toResult), makeExpected, "positive idempotent conversion");
    validate(-1000, xform(ident, toResult), makeExpected, "negative idempotent conversion");
    validate(0, xform(ident, toResult), makeExpected, "zero idempotent conversion");
    validate(1000, xform(toDouble, toResult), makeExpected, "positive double conversion");
    validate(-1000, xform(toDouble, toResult), makeExpected, "negative double conversion");
    validate(0, xform(toDouble, toResult), makeExpected, "zero double conversion");
    validate(0, xform(toURep, toResult), makeExpected, "zero unsigned conversion");
}

TEST_F(DurationCast, HigherPrecisionCoerce) {
    auto makeExpected = [](Rep x) { return Milliseconds(x * 1000); };
    auto toResult = CoerceFunction<Seconds>{};

    validate(1000, xform(ident, toResult), makeExpected, "positive idempotent conversion");
    validate(-1000, xform(ident, toResult), makeExpected, "negative idempotent conversion");
    validate(0, xform(ident, toResult), makeExpected, "zero idempotent conversion");
    validate(1000, xform(toDouble, toResult), makeExpected, "positive double conversion");
    validate(-1000, xform(toDouble, toResult), makeExpected, "negative double conversion");
    validate(0, xform(toDouble, toResult), makeExpected, "zero double conversion");
    validate(0, xform(toURep, toResult), makeExpected, "zero unsigned conversion");
}

TEST_F(DurationCast, LowerPrecisionCoerce) {
    auto makeExpected = [](Rep x) { return Milliseconds(x / 1000); };
    auto toResult = CoerceFunction<Microseconds>{};

    validate(1000, xform(ident, toResult), makeExpected, "positive idempotent conversion");
    validate(-1000, xform(ident, toResult), makeExpected, "negative idempotent conversion");
    validate(0, xform(ident, toResult), makeExpected, "zero idempotent conversion");
    validate(1000, xform(toDouble, toResult), makeExpected, "positive double conversion");
    validate(-1000, xform(toDouble, toResult), makeExpected, "negative double conversion");
    validate(0, xform(toDouble, toResult), makeExpected, "zero double conversion");
    validate(0, xform(toURep, toResult), makeExpected, "zero unsigned conversion");
}

TEST_F(DurationCast, BadCoerce) {
    auto assertThrows = [&](auto val, StringData msg) {
        auto chronoVal = deduceChronoDuration<std::milli>(val);
        Milliseconds out{};
        ASSERT_THROWS_CODE(
            out = duration_cast<Milliseconds>(chronoVal), DBException, ErrorCodes::BadValue)
            << msg
            << std::fixed
            << ", val:" << val
            << ", out:" << out;
    };
    auto floatTrial = [&](auto tag) {
        using F = typename decltype(tag)::type;
        using FLimits = std::numeric_limits<F>;
        using RepLimits = std::numeric_limits<Rep>;

        static constexpr auto kRepMax = RepLimits::max();
        static constexpr auto kRepMin = RepLimits::min();

        // A few useful F values.
        static const auto fInf = FLimits::infinity();
        static const auto fMaxPower = std::scalbn(F{1}, RepLimits::digits);
        static const auto fRepMax = static_cast<F>(kRepMax);
        static const auto fRepMin = static_cast<F>(kRepMin);

        auto fmtMsg = [&](auto msg) { return "(with F={}) {}"_format(demangleName(typeid(F)), msg); };
        assertThrows(FLimits::quiet_NaN(), fmtMsg("floating NaN"));
        assertThrows(fInf, fmtMsg("+inf"));
        assertThrows(-fInf, fmtMsg("-inf"));

        auto exceedsRange = [](F v) { return v >= fMaxPower || v < -fMaxPower; };

        if (F v = std::nexttoward(fRepMax, fInf); exceedsRange(v))
            assertThrows(v, fmtMsg("floating = max+epsilon"));

        if (F v = std::nexttoward(fRepMin, -fInf); exceedsRange(v))
            assertThrows(v, fmtMsg("floating = min-epsilon"));

        assertThrows(1.5 * fRepMax, fmtMsg("floating = 1.5*max"));
        assertThrows(1.5 * fRepMin, fmtMsg("floating = 1.5*min"));
        assertThrows(2 * fRepMax, fmtMsg("floating = 2*max"));
        assertThrows(2 * fRepMin, fmtMsg("floating = 2*min"));
    };
    floatTrial(stdx::type_identity<float>{});
    floatTrial(stdx::type_identity<double>{});
    floatTrial(stdx::type_identity<long double>{});

    assertThrows(std::numeric_limits<URep>::max(), "Uint too large");
}

}  // namespace
}  // namespace mongo
