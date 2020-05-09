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

#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <numeric>
#include <ostream>
#include <ratio>
#include <type_traits>

#include <fmt/format.h>

#include "mongo/base/static_assert.h"
#include "mongo/platform/overflow_arithmetic.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/str.h"

namespace mongo {

class BSONObj;

template <typename Period>
class Duration;

using Nanoseconds = Duration<std::nano>;
using Microseconds = Duration<std::micro>;
using Milliseconds = Duration<std::milli>;
using Seconds = Duration<std::ratio<1>>;
using Minutes = Duration<std::ratio<60>>;
using Hours = Duration<std::ratio<3600>>;
using Days = Duration<std::ratio<86400>>;

namespace duration_detail {

/** True only for specializations of mongo::Duration. */
template <typename>
inline constexpr bool isMongoDuration = false;
template <typename... Ts>
inline constexpr bool isMongoDuration<Duration<Ts...>> = true;

/** True only for specializations of std::chrono::duration. */
template <typename>
inline constexpr bool isStdChronoDuration = false;
template <typename... Ts>
inline constexpr bool isStdChronoDuration<std::chrono::duration<Ts...>> = true;


template <typename T, typename U>
constexpr T narrow(U u) {
    T t = static_cast<T>(u);
    if constexpr (!std::chrono::treat_as_floating_point_v<T> &&
                  !std::chrono::treat_as_floating_point_v<U>) {
        if ((u != static_cast<U>(t)) ||  // Round trip failure
            ((std::is_signed_v<T> != std::is_signed_v<U>)&&((t < T{}) !=
                                                            (u < U{})))) {  // Sign warped
            uasserted(ErrorCodes::BadValue, "Lossy narrowing");
        }
    } else if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        if (!std::isfinite(u))  // includes NaN and infinite values.
            uasserted(ErrorCodes::BadValue, "Undefined: non-finite to integral");
        // Lossy float to integral conversion is ok, but not range violations.
        int exp{};
        (void)std::frexp(u, &exp);  // unused fractional part in +/- [.5,1).
        if (exp > std::numeric_limits<T>::digits)
            uasserted(ErrorCodes::BadValue, "Undefined: floating point beyond integral range");
    }
    return t;
}

template <typename To, typename From>
inline constexpr bool usableDurationCastArgs = isMongoDuration<To> &&
    (isStdChronoDuration<From> || isMongoDuration<From>);

template <typename To, typename From, typename = void>
struct CastPolicy {};

template <typename To, typename From>
struct CastPolicy<To, From, std::enable_if_t<usableDurationCastArgs<To, From>>> {
    using type = CastPolicy;
    using Conv = std::ratio_divide<typename From::period, typename To::period>;
    using Common = std::common_type_t<std::intmax_t, typename From::rep, typename To::rep>;
};

template <typename To, typename From>
using CastPolicyT = typename CastPolicy<To, From>::type;

}  // namespace duration_detail

/**
 * Casts from one Duration precision to another.
 * Works universally to convert among and between mongo::Duration and std::chrono::duration.
 *
 * May throw a DurationOverflow DBException if `from` is of lower-precision type and is
 * outside the range of the ToDuration. For example, Seconds::max() cannot be represented as a
 * Milliseconds, so `duration_cast<Milliseconds>(Seconds::max())` throws.
 */
template <typename To, typename From, typename Policy = duration_detail::CastPolicyT<To, From>>
constexpr To duration_cast(const From& from) {
    using Conv = typename Policy::Conv;
    using Common = typename Policy::Common;
    auto r = static_cast<Common>(from.count());
    if constexpr (Conv::num > 1) {
        if constexpr (std::chrono::treat_as_floating_point_v<Common>) {
            r *= Conv::num;
        } else {
            if (overflow::mul(r, static_cast<Common>(Conv::num), &r))
                uasserted(ErrorCodes::DurationOverflow,
                          "Overflow casting from a lower-precision duration "
                          "to a higher-precision duration");
        }
    }

    if constexpr (Conv::den > 1)
        r /= static_cast<Common>(Conv::den);

    return To(duration_detail::narrow<typename To::rep>(r));
}

/**
 * Type representing a duration using a 64-bit counter.
 *
 * The Period template argument is a std::ratio describing the units of the duration type.
 *
 * This type's behavior is similar to std::chrono::duration, but instead of undefined behavior on
 * overflows and other conversions, throws exceptions.
 */
template <typename Period>
class Duration {
public:
    static constexpr StringData unit_short();
    static constexpr StringData mongoUnitSuffix();

    MONGO_STATIC_ASSERT_MSG(Period::num > 0, "Duration::period's numerator must be positive");
    MONGO_STATIC_ASSERT_MSG(Period::den > 0, "Duration::period's denominator must be positive");

    using rep = int64_t;
    using period = Period;

    static constexpr Duration zero() {
        return Duration{};
    }

    static constexpr Duration min() {
        return Duration{std::numeric_limits<rep>::min()};
    }

    static constexpr Duration max() {
        return Duration{std::numeric_limits<rep>::max()};
    }

    /**
     * Constructs the zero duration.
     */
    constexpr Duration() = default;

    /**
     * Constructs a duration representing "r" periods.
     */
    template <typename T,
              std::enable_if_t<std::is_convertible_v<T, rep> && std::is_integral_v<T>, int> = 0>
    constexpr explicit Duration(const T& r) : _count{r} {
        MONGO_STATIC_ASSERT_MSG(
            std::is_signed_v<T> || sizeof(T) < sizeof(rep),
            "Durations must be constructed from values of integral type that are "
            "representable as 64-bit signed integers");
    }

    /**
     * Implicit converting constructor from a lower-precision duration to a higher-precision one, as
     * by duration_cast.
     *
     * It is a compilation error to convert from higher precision to lower.
     * Throws if the conversion causes an integer overflow.
     */
    template <typename FromPeriod,
              typename FromDuration = Duration<FromPeriod>,
              typename Common = std::common_type_t<Duration, FromDuration>,
              std::enable_if_t<std::is_same_v<Common, Duration>, int> = 0>
    constexpr Duration(const Duration<FromPeriod>& from)
        : Duration(duration_cast<Duration>(from)) {}

    constexpr std::chrono::system_clock::duration toSystemDuration() const {
        using SystemDuration = std::chrono::system_clock::duration;
        return SystemDuration{duration_cast<Duration<SystemDuration::period>>(*this).count()};
    }

    /**
     * Returns the number of periods represented by this duration.
     *
     * It is better to use durationCount<DesiredDurationType>(value), since it makes the unit of the
     * count clear at the call site.
     */
    constexpr rep count() const {
        return _count;
    }

    /**
     * Compares this duration to another duration of the same type.
     *
     * Returns {-1, 0, 1} when this is {less, equal, greater} than other, respectively.
     */
    constexpr int compare(const Duration& other) const {
        return [](auto x, auto y) { return x < y ? -1 : x > y ? 1 : 0; }(count(), other.count());
    }

    /**
     * Compares this duration to another duration of a different period.
     *
     * Returns {-1, 0, 1} when this is {less, equal, greater} than other, respectively.
     */
    template <typename Per2>
    constexpr int compare(const Duration<Per2>& other) const {
        using C = std::common_type_t<Duration, Duration<Per2>>;
        using CR = typename C::rep;
        using CP = typename C::period;
        if (CR c = count(); overflow::mul(c, std::ratio_divide<period, CP>::num, &c))
            return compare(zero());
        if (CR c = other.count(); overflow::mul(c, std::ratio_divide<Per2, CP>::num, &c))
            return -other.compare(Duration<Per2>::zero());
        return C{*this}.compare(C{other});
    }

    constexpr Duration operator+() const {
        return *this;
    }

    constexpr Duration operator-() const {
        if (*this == min())
            uasserted(ErrorCodes::DurationOverflow, "Cannot negate the minimum duration");
        return Duration(-count());
    }

    constexpr Duration& operator++() {
        return *this += Duration{1};
    }

    constexpr Duration& operator--() {
        return *this -= Duration{1};
    }

    constexpr Duration operator++(int) {
        auto result = *this;
        ++*this;
        return result;
    }

    constexpr Duration operator--(int) {
        auto result = *this;
        *this -= Duration{1};
        return result;
    }

    constexpr Duration& operator+=(const Duration& other) {
        if (overflow::add(_count, other.count(), &_count))
            uasserted(ErrorCodes::DurationOverflow,
                      fmt::format("Overflow while adding {} to {}", other, *this));
        return *this;
    }

    constexpr Duration& operator-=(const Duration& other) {
        if (overflow::sub(count(), other.count(), &_count))
            uasserted(ErrorCodes::DurationOverflow,
                      fmt::format("Overflow while subtracting {} from {}", other, *this));
        return *this;
    }

    template <typename T>
    constexpr Duration& operator*=(const T& scale) {
        MONGO_STATIC_ASSERT_MSG(
            std::is_integral_v<T> && std::is_signed_v<T>,
            "Durations may only be multiplied by values of signed integral type");
        if (overflow::mul(count(), scale, &_count))
            uasserted(ErrorCodes::DurationOverflow,
                      fmt::format("Overflow while multiplying {} by {}", *this, scale));
        return *this;
    }

    template <typename T>
    constexpr Duration& operator/=(const T& scale) {
        MONGO_STATIC_ASSERT_MSG(std::is_integral_v<T> && std::is_signed_v<T>,
                                "Durations may only be divided by values of signed integral type");
        if (!(count() != min().count() || scale != -1)) {
            uasserted(ErrorCodes::DurationOverflow,
                      fmt::format("Overflow while dividing {} by {}", *this, scale));
        }
        _count /= scale;
        return *this;
    }

    BSONObj toBSON() const;

    std::string toString() const {
        return fmt::format("{}", *this);
    }

private:
    rep _count = {};
};

template <typename P1, typename P2>
constexpr bool operator==(const Duration<P1>& a, const Duration<P2>& b) {
    return a.compare(b) == 0;
}

template <typename P1, typename P2>
constexpr bool operator!=(const Duration<P1>& a, const Duration<P2>& b) {
    return a.compare(b) != 0;
}

template <typename P1, typename P2>
constexpr bool operator<(const Duration<P1>& a, const Duration<P2>& b) {
    return a.compare(b) < 0;
}

template <typename P1, typename P2>
constexpr bool operator<=(const Duration<P1>& a, const Duration<P2>& b) {
    return a.compare(b) <= 0;
}

template <typename P1, typename P2>
constexpr bool operator>(const Duration<P1>& a, const Duration<P2>& b) {
    return a.compare(b) > 0;
}

template <typename P1, typename P2>
constexpr bool operator>=(const Duration<P1>& a, const Duration<P2>& b) {
    return a.compare(b) >= 0;
}

template <typename P1, typename P2, typename C = std::common_type_t<Duration<P1>, Duration<P2>>>
constexpr C operator+(const Duration<P1>& a, const Duration<P2>& b) {
    return C{a} += C{b};
}

template <typename P1, typename P2, typename C = std::common_type_t<Duration<P1>, Duration<P2>>>
constexpr C operator-(const Duration<P1>& a, const Duration<P2>& b) {
    return C{a} -= C{b};
}

template <typename P, typename T>
constexpr Duration<P> operator*(Duration<P> d, const T& scale) {
    return d *= scale;
}

template <typename T, typename P>
constexpr Duration<P> operator*(const T& scale, Duration<P> d) {
    return d *= scale;
}

template <typename P, typename T>
constexpr Duration<P> operator/(Duration<P> d, const T& scale) {
    return d /= scale;
}

namespace duration_detail {
template <typename Stream, typename P>
Stream& streamPut(Stream& os, const Duration<P>& dp) {
    MONGO_STATIC_ASSERT_MSG(!Duration<P>::unit_short().empty(),
                            "Only standard Durations can be logged");
    return os << dp.count() << dp.unit_short();
}

template <typename>
inline constexpr StringData kUnitShort{};
template <>
inline constexpr StringData kUnitShort<Nanoseconds> = "ns"_sd;
template <>
inline constexpr StringData kUnitShort<Microseconds> =
    "\u03bcs"_sd;  // GREEK SMALL LETTER MU (μ) + "s"
template <>
inline constexpr StringData kUnitShort<Milliseconds> = "ms"_sd;
template <>
inline constexpr StringData kUnitShort<Seconds> = "s"_sd;
template <>
inline constexpr StringData kUnitShort<Minutes> = "min"_sd;
template <>
inline constexpr StringData kUnitShort<Hours> = "hr"_sd;
template <>
inline constexpr StringData kUnitShort<Days> = "d"_sd;

template <typename>
inline constexpr StringData kUnitSuffix{};
template <>
inline constexpr StringData kUnitSuffix<Nanoseconds> = "Nanos"_sd;
template <>
inline constexpr StringData kUnitSuffix<Microseconds> = "Micros"_sd;
template <>
inline constexpr StringData kUnitSuffix<Milliseconds> = "Millis"_sd;
template <>
inline constexpr StringData kUnitSuffix<Seconds> = "Seconds"_sd;
template <>
inline constexpr StringData kUnitSuffix<Minutes> = "Minutes"_sd;
template <>
inline constexpr StringData kUnitSuffix<Hours> = "Hours"_sd;
template <>
inline constexpr StringData kUnitSuffix<Days> = "Days"_sd;
}  // namespace duration_detail

template <typename Period>
constexpr StringData Duration<Period>::unit_short() {
    return duration_detail::kUnitShort<Duration>;
}

template <typename Period>
constexpr StringData Duration<Period>::mongoUnitSuffix() {
    return duration_detail::kUnitSuffix<Duration>;
}

template <typename Period>
std::ostream& operator<<(std::ostream& os, Duration<Period> dp) {
    return duration_detail::streamPut(os, dp);
}

template <typename Period>
StringBuilder& operator<<(StringBuilder& os, Duration<Period> dp) {
    return duration_detail::streamPut(os, dp);
}

template <typename Period>
StackStringBuilder& operator<<(StackStringBuilder& os, Duration<Period> dp) {
    return duration_detail::streamPut(os, dp);
}

/**
 * Convenience method for reading the count of a duration with specified units.
 *
 * Use when logging or comparing to integers, to ensure that you're using
 * the units you intend.
 *
 * E.g., log() << durationCount<Seconds>(some duration) << " seconds";
 *
 */
template <typename DOut, typename DIn>
long long durationCount(DIn d) {
    return duration_cast<DOut>(d).count();
}

template <typename DOut, typename RepIn, typename PeriodIn>
long long durationCount(const std::chrono::duration<RepIn, PeriodIn>& d) {
    return durationCount<DOut>(Duration<PeriodIn>{d.count()});
}

/**
 * Make a std::chrono::duration from an arithmetic expression and a period ratio.
 * This does not do any math or precision changes. It's just a type-deduced wrapper.
 * The output std::chrono::duration will retain the Rep type of the input argument.
 *
 * E.g:
 *      int waitedMsec;  // unitless, type-unsafe number.
 *      auto waitedDur = deduceChronoDuration<std::millis>(waitedMsec);
 *      static_assert(std::is_same_v<decltype(waitedDur),
 *                                   std::chrono::duration<int,std::millis>>);
 *
 * From there, mongo::duration_cast can bring it into the mongo::Duration system if desired.
 *
 *      auto waitedMilliseconds = mongo::duration_cast<mongo::Milliseconds>(waitedDur);
 *
 * Or std::chrono::duration_cast be used to adjust the rep value and type to create a
 * standard duration (like std::chrono::milliseconds).
 */
template <typename Per = std::ratio<1>, typename Rep>
constexpr auto deduceChronoDuration(const Rep& count) {
    return std::chrono::duration<Rep, Per>{count};
}

}  // namespace mongo

namespace fmt {
template <typename Per>
struct formatter<mongo::Duration<Per>> {
    template <typename Ctx>
    constexpr auto parse(Ctx& ctx) {
        return ctx.begin();
    }
    template <typename Ctx>
    auto format(const mongo::Duration<Per>& d, Ctx& ctx) {
        return format_to(ctx.out(), "{} {}", d.count(), d.unit_short());
    }
};
}  // namespace fmt

namespace std {

/**
 * Define the std::common_type between mongo::Duration classes, a type
 * that can represent quantities from either input type. It's the type
 * used for expressions like `Duration<P1>{} + Duration<P2>{}`.
 * See https://en.cppreference.com/w/cpp/chrono/duration/common_type.
 * The resulting Duration's period is the greatest common divisor of the
 * input Duration periods.
 *
 * We need a common period in which both durations can be losslessly, but minimally, expressed.
 *
 *      gcd(n1, n2) / lcm(d1, d2)
 */
template <typename P1, typename P2>
struct common_type<mongo::Duration<P1>, mongo::Duration<P2>> {
    using type = mongo::Duration<std::ratio_divide<std::ratio<std::gcd(P1::num, P2::num)>,
                                                   std::ratio<std::lcm(P1::den, P2::den)>>>;
};

}  // namespace std
