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

#include <cstdint>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <iosfwd>
#include <limits>
#include <ratio>

#include "mongo/base/static_assert.h"
#include "mongo/bson/util/builder.h"
#include "mongo/platform/overflow_arithmetic.h"
#include "mongo/stdx/chrono.h"
#include "mongo/stdx/type_traits.h"
#include "mongo/util/assert_util.h"

namespace mongo {

/**
 * Represents a duration using a 64-bit counter.
 *
 * The `Period` is a `std::ratio` specifying granularity in seconds.
 *
 * This type's behavior is similar to std::chrono::duration, but instead of undefined behavior on
 * overflows and other conversions, throws exceptions.
 */
template <typename Period>
class Duration {
public:
    static constexpr StringData unit_short() {
        if constexpr (std::is_same_v<Duration, Nanoseconds>) {
            return "ns"_sd;
        } else if constexpr (std::is_same_v<Duration, Microseconds>) {
            return "\xce\xbcs"_sd;
        } else if constexpr (std::is_same_v<Duration, Milliseconds>) {
            return "ms"_sd;
        } else if constexpr (std::is_same_v<Duration, Seconds>) {
            return "s"_sd;
        } else if constexpr (std::is_same_v<Duration, Minutes>) {
            return "min"_sd;
        } else if constexpr (std::is_same_v<Duration, Hours>) {
            return "hr"_sd;
        } else if constexpr (std::is_same_v<Duration, Days>) {
            return "d"_sd;
        }
        return StringData{};
    }
    static constexpr StringData mongoUnitSuffix() {
        if constexpr (std::is_same_v<Duration, Nanoseconds>) {
            return "Nanos"_sd;
        } else if constexpr (std::is_same_v<Duration, Microseconds>) {
            return "Micros"_sd;
        } else if constexpr (std::is_same_v<Duration, Milliseconds>) {
            return "Millis"_sd;
        } else if constexpr (std::is_same_v<Duration, Seconds>) {
            return "Seconds"_sd;
        } else if constexpr (std::is_same_v<Duration, Minutes>) {
            return "Minutes"_sd;
        } else if constexpr (std::is_same_v<Duration, Hours>) {
            return "Hours"_sd;
        } else if constexpr (std::is_same_v<Duration, Days>) {
            return "Days"_sd;
        }
        return StringData{};
    }
    MONGO_STATIC_ASSERT_MSG(Period::num > 0, "Duration::period's numerator must be positive");
    MONGO_STATIC_ASSERT_MSG(Period::den > 0, "Duration::period's denominator must be positive");

    using rep = int64_t;
    using period = Period;

private:
    template <typename T>
    using RequireScalar = std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>>*;

public:
    template <typename FromPeriod>
    static constexpr Duration castFrom(Duration<FromPeriod> from) {
        assertCompatiblePeriod<FromPeriod>();
        // Let r = (from.count() * FromPeriod) / period
        rep r = from.count();
        using ConvFactor = std::ratio_divide<FromPeriod, period>;
        if constexpr (ConvFactor::num > 1) {
            uassert(ErrorCodes::DurationOverflow,
                    "Overflow casting from a lower- to higher-precision duration",
                    !overflow::mul(r, ConvFactor::num, &r));
        }
        if constexpr (ConvFactor::den > 1) {
            r /= ConvFactor::den;
        }
        return Duration{r};
    }

    static constexpr Duration zero() {
        return Duration{};
    }

    static constexpr Duration min() {
        return Duration{std::numeric_limits<rep>::min()};
    }

    static constexpr Duration max() {
        return Duration{std::numeric_limits<rep>::max()};
    }

    /** The zero duration. */
    constexpr Duration() = default;

    /** A Duration representing `count` periods. */
    template <typename T,
              std::enable_if_t<std::is_convertible_v<T, rep> && std::is_integral_v<T> &&
                               (std::is_signed_v<T> || sizeof(T) < sizeof(rep))>* = nullptr>
    constexpr explicit Duration(T count) : _count(count) {}

    /**
     * Implicit conversion from a lower- to higher-precision duration as with duration_cast.
     * The conversion must not cause an integer overflow.
     */
    template <typename FromPeriod>
    constexpr Duration(Duration<FromPeriod> from) : Duration{castFrom(from)} {
        MONGO_STATIC_ASSERT_MSG(std::ratio_less_v<period, FromPeriod>,
                                "Use duration_cast to convert higher- to lower-precision"
                                " Duration types");
    }

    stdx::chrono::system_clock::duration toSystemDuration() const {
        using SystemDuration = stdx::chrono::system_clock::duration;
        return SystemDuration{Duration<SystemDuration::period>::castFrom(*this).count()};
    }

    /**
     * Periods represented by this duration.
     *
     * It is better to use durationCount<DesiredDurationType>(value), since it makes the unit of the
     * count clear at the call site.
     */
    constexpr rep count() const {
        return _count;
    }

    /**
     * Compares this duration to another of the same type.
     *
     * Returns 1, -1 or 0 depending on whether this duration is greater than, less than or equal to
     * the other duration, respectively.
     */
    constexpr int compare(Duration other) const {
        return (count() > other.count()) ? 1 : (count() < other.count()) ? -1 : 0;
    }

    /**
     * Compares this duration to another.
     */
    template <typename OtherPeriod>
    int compare(Duration<OtherPeriod> other) const {
        assertCompatiblePeriod<OtherPeriod>();
        if (std::ratio_less_v<OtherPeriod, period>) {
            return -other.compare(*this);  // swap args to work in higher precision
        }
        // `other` now has equal or lower precision.
        // `oc` is the count that the other Duration would have in this (higher) precision:
        //  Let oc = (other.count() * OtherPeriod) / period
        using Scale = std::ratio_divide<OtherPeriod, period>;
        rep oc = other.count();
        if constexpr (Scale::num > 1) {
            if (overflow::mul(oc, Scale::num, &oc)) {
                // `oc * Scale::num` would overflow
                return other.count() < 0 ? 1 : -1;
            }
        }
        if constexpr (Scale::den > 1) {
            oc /= Scale::den;
        }
        return count() > oc ? 1 : count() < oc ? -1 : 0;
    }

    constexpr Duration operator+() const {
        return *this;
    }

    Duration operator-() const {
        uassert(ErrorCodes::DurationOverflow, "Cannot negate the minimum duration", *this != min());
        return Duration(-count());
    }

    Duration& operator+=(Duration other) {
        _count = add(_count, other._count);
        return *this;
    }

    Duration& operator-=(Duration other) {
        _count = sub(_count, other._count);
        return *this;
    }

    template <typename T, RequireScalar<T> = nullptr>
    Duration& operator*=(const T& scale) {
        _count = mul(_count, scale);
        return *this;
    }

    template <typename T, RequireScalar<T> = nullptr>
    Duration& operator/=(const T& scale) {
        using namespace fmt::literals;
        uassert(ErrorCodes::DurationOverflow,
                "Overflow while dividing {} by -1"_format(*this),
                (count() != min().count() || scale != -1));
        _count /= scale;
        return *this;
    }


    auto& operator++() {
        return *this += Duration{1};
    }
    auto& operator--() {
        return *this -= Duration{1};
    }
    auto operator++(int) {
        auto r = *this;
        ++*this;
        return r;
    }
    auto operator--(int) {
        auto r = *this;
        ++*this;
        return r;
    }

    template <typename T, RequireScalar<T> = nullptr>
    friend auto operator*(Duration d, T scale) {
        return d *= scale;
    }
    template <typename T, RequireScalar<T>* = nullptr>
    friend auto operator*(T scale, Duration d) {
        return d *= scale;
    }

    template <typename T, RequireScalar<T>* = nullptr>
    friend auto operator/(Duration d, T scale) {
        return d /= scale;
    }

    template <typename OtherDuration>
    using ChooseHigherPrecision =
        std::conditional_t<std::ratio_less_v<period, typename OtherDuration::period>,
                           Duration,
                           OtherDuration>;

private:
    MONGO_STATIC_ASSERT_MSG(Period::num > 0, "Duration::period's numerator must be positive");
    MONGO_STATIC_ASSERT_MSG(Period::den > 0, "Duration::period's denominator must be positive");

    template <typename P>
    static constexpr void assertCompatiblePeriod() {
        using Q = std::ratio_divide<period, P>;
        MONGO_STATIC_ASSERT_MSG((Q::num == 1 || Q::den == 1),
                                "mongo::Duration types are compatible only when one period "
                                "is a multiple of the other.");
    }

    static rep add(rep a, rep b) {
        rep r;
        if (overflow::add(a, b, &r)) {
            using namespace fmt::literals;
            uasserted(ErrorCodes::DurationOverflow, "adding {} and {}"_format(a, b));
        }
        return r;
    }
    static rep sub(rep a, rep b) {
        rep r;
        if (overflow::sub(a, b, &r)) {
            using namespace fmt::literals;
            uasserted(ErrorCodes::DurationOverflow, "subtracting {1} from {0}"_format(a, b));
        }
        return r;
    }
    static rep mul(rep a, rep b) {
        rep r;
        if (overflow::mul(a, b, &r)) {
            using namespace fmt::literals;
            uasserted(ErrorCodes::DurationOverflow, "multiplying {} by {}"_format(a, b));
        }
        return r;
    }

    rep _count = 0;
};

template <typename AP, typename BP>
constexpr bool operator==(Duration<AP> a, Duration<BP> b) {
    return a.compare(b) == 0;
}

template <typename AP, typename BP>
constexpr bool operator!=(Duration<AP> a, Duration<BP> b) {
    return a.compare(b) != 0;
}

template <typename AP, typename BP>
constexpr bool operator<(Duration<AP> a, Duration<BP> b) {
    return a.compare(b) < 0;
}

template <typename AP, typename BP>
constexpr bool operator<=(Duration<AP> a, Duration<BP> b) {
    return a.compare(b) <= 0;
}

template <typename AP, typename BP>
constexpr bool operator>(Duration<AP> a, Duration<BP> b) {
    return a.compare(b) > 0;
}

template <typename AP, typename BP>
constexpr bool operator>=(Duration<AP> a, Duration<BP> b) {
    return a.compare(b) >= 0;
}

/** Returns the sum of two durations in whichever type has higher precision. */
template <typename AP, typename BP>
auto operator+(Duration<AP> a, Duration<BP> b) {
    using HP = typename Duration<AP>::template ChooseHigherPrecision<Duration<BP>>;
    HP r{a};
    return r += b;
}

/** Returns the difference of two durations in whichever type has higher precision. */
template <typename AP, typename BP>
auto operator-(Duration<AP> a, Duration<BP> b) {
    using HP = typename Duration<AP>::template ChooseHigherPrecision<Duration<BP>>;
    HP r{a};
    return r -= b;
}

namespace duration_detail {
inline decltype(auto) streamCast(std::ostream& os) {
    return os;
}
inline decltype(auto) streamCast(StringBuilder& os) {
    return os;
}
inline decltype(auto) streamCast(StackStringBuilder& os) {
    return os;
}
}  // namespace duration_detail

/**
 * Casts from one Duration precision to another.
 *
 * May throw a AssertionException if "from" is of lower-precision type and is outside the range of
 * the ToDuration. For example, Seconds::max() cannot be represented as a Milliseconds, and so
 * attempting to cast that value to Milliseconds will throw an exception.
 */
template <typename ToDuration, typename FromPeriod>
constexpr ToDuration duration_cast(Duration<FromPeriod> from) {
    return ToDuration::castFrom(from);
}

template <typename ToDuration, typename... A>
constexpr ToDuration duration_cast(stdx::chrono::duration<A...> d) {
    using FromPeriod = typename decltype(d)::period;
    return duration_cast<ToDuration>(Duration<FromPeriod>{d.count()});
}

/**
 * Read the count of a duration with specified units.
 *
 * Use when logging or comparing to integers, to ensure that you're using
 * the units you intend.
 *
 * E.g., log() << durationCount<Seconds>(some duration) << " seconds";
 */
template <typename ToDuration, typename P>
constexpr long long durationCount(Duration<P> d) {
    return duration_cast<ToDuration>(d).count();
}
template <typename ToDuration, typename... A>
constexpr long long durationCount(stdx::chrono::duration<A...> d) {
    return duration_cast<ToDuration>(d).count();
}

using Nanoseconds = Duration<std::nano>;
using Microseconds = Duration<std::micro>;
using Milliseconds = Duration<std::milli>;
using Seconds = Duration<std::ratio<1>>;
using Minutes = Duration<std::ratio<60>>;
using Hours = Duration<std::ratio<3600>>;
using Days = Duration<std::ratio<86400>>;

template <typename Duration>
constexpr StringData durationSuffix;
template <>
constexpr StringData durationSuffix<Nanoseconds> = "ns"_sd;
template <>
constexpr StringData durationSuffix<Microseconds> = u8"μs"_sd; /* "μ" == "\u03bc" */
template <>
constexpr StringData durationSuffix<Milliseconds> = "ms"_sd;
template <>
constexpr StringData durationSuffix<Seconds> = "s"_sd;
template <>
constexpr StringData durationSuffix<Minutes> = "min"_sd;
template <>
constexpr StringData durationSuffix<Hours> = "hr"_sd;
template <>
constexpr StringData durationSuffix<Days> = "d"_sd;

}  // namespace mongo

namespace fmt {
template <typename P>
struct formatter<mongo::Duration<P>> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(const ::mongo::Duration<P>& x, FormatContext& ctx) {
        return format_to(ctx.out(), "{}{}", x.count(), ::mongo::durationSuffix<decltype(x)>);
    }
};
}  // namespace fmt

namespace mongo {
// Streaming output operator for common duration types and common streamlike types. Writes the
// numerical value followed by a units suffix. E.g., `std::cout << Minutes{5}` produces "5min".
template <typename Stream,
          typename P,
          typename = decltype(duration_detail::streamCast(std::declval<Stream&>()))>
decltype(auto) operator<<(Stream& os, Duration<P> dur) {
    using namespace fmt::literals;
    return duration_detail::streamCast(os) << "{}"_format(dur);
}
}  // namespace mongo
