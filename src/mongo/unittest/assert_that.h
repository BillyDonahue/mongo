/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

#include <algorithm>
#include <fmt/format.h>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "mongo/base/string_data.h"
#include "mongo/bson/bsonelement.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/unittest/unittest.h"

namespace mongo::unittest::match {

class MatchResult {
public:
    MatchResult() : MatchResult(true) {}
    explicit MatchResult(bool ok) : MatchResult(ok, "") {}
    MatchResult(bool ok, std::string msg) : _ok(ok), _msg(std::move(msg)) {}
    explicit operator bool() const {
        return _ok;
    }
    const std::string& message() const {
        return _msg;
    }

private:
    bool _ok = false;
    std::string _msg;
};

class Matcher {};

namespace detail {

template <typename T>
std::string stringifyForAssert(const T& x);

template <typename T>
std::string doFormat(const T& x) {
    return format(FMT_STRING("{}"), x);
}

template <typename T>
std::string doOstream(const T& x) {
    std::ostringstream os;
    os << x;
    return os.str();
}

// TS2's `std::experimental::detect`:
template <typename Default, typename AlwaysVoid, template <class...> class Op, typename... Args>
struct Detector {
    using value_t = std::false_type;
    using type = Default;
};
template <typename Default, template <class...> class Op, typename... Args>
struct Detector<Default, std::void_t<Op<Args...>>, Op, Args...> {
    using value_t = std::true_type;
    using type = Op<Args...>;
};
struct Nonesuch {
    Nonesuch() = delete;
    ~Nonesuch() = delete;
    Nonesuch(const Nonesuch&) = delete;
    Nonesuch& operator=(const Nonesuch&) = delete;
};
template <template <class...> class Op, typename... Args>
using IsDetected = typename Detector<Nonesuch, void, Op, Args...>::value_t;
template <template <class...> class Op, typename... Args>
constexpr bool IsDetectedV = IsDetected<Op, Args...>::value;

template <size_t N, typename X>
auto tieStruct(const X& x) {
    /*
     * There are no variadic structured bindings. In the meantime, this
     * function body can be tweaked and regenerated by this Python snippet:
    =====
    #!/usr/bin/env python
    N = 10
    print("    if constexpr (N == 0) {")
    print("        return std::tie();")
    for n in range(1, N):
        fs = ["f{}".format(j) for j in range(n)]
        print("    }} else if constexpr (N == {}) {{".format(n))
        print("        const auto& [{}] = x;".format(",".join(fs)))
        print("        return std::tie({});".format(",".join(fs)))
    print("    }")
    =====
    */
    if constexpr (N == 0) {
        return std::tie();
    } else if constexpr (N == 1) {
        const auto& [f0] = x;
        return std::tie(f0);
    } else if constexpr (N == 2) {
        const auto& [f0, f1] = x;
        return std::tie(f0, f1);
    } else if constexpr (N == 3) {
        const auto& [f0, f1, f2] = x;
        return std::tie(f0, f1, f2);
    } else if constexpr (N == 4) {
        const auto& [f0, f1, f2, f3] = x;
        return std::tie(f0, f1, f2, f3);
    } else if constexpr (N == 5) {
        const auto& [f0, f1, f2, f3, f4] = x;
        return std::tie(f0, f1, f2, f3, f4);
    } else if constexpr (N == 6) {
        const auto& [f0, f1, f2, f3, f4, f5] = x;
        return std::tie(f0, f1, f2, f3, f4, f5);
    } else if constexpr (N == 7) {
        const auto& [f0, f1, f2, f3, f4, f5, f6] = x;
        return std::tie(f0, f1, f2, f3, f4, f5, f6);
    } else if constexpr (N == 8) {
        const auto& [f0, f1, f2, f3, f4, f5, f6, f7] = x;
        return std::tie(f0, f1, f2, f3, f4, f5, f6, f7);
    } else if constexpr (N == 9) {
        const auto& [f0, f1, f2, f3, f4, f5, f6, f7, f8] = x;
        return std::tie(f0, f1, f2, f3, f4, f5, f6, f7, f8);
    }
    MONGO_UNREACHABLE;
}

using std::begin;
using std::end;

template <typename T>
using HasToStringOp = decltype(std::declval<T>().toString());
template <typename T>
constexpr bool HasToString = IsDetectedV<HasToStringOp, T>;

template <typename T>
using HasOstreamOp = decltype(std::declval<std::ostream>() << std::declval<T>());
template <typename T>
constexpr bool HasOstream = IsDetectedV<HasOstreamOp, T>;

template <typename T>
using HasBeginEndOp =
    std::tuple<decltype(begin(std::declval<T>())), decltype(end(std::declval<T>()))>;
template <typename T>
constexpr bool IsSequence = IsDetectedV<HasBeginEndOp, T>;

class Joiner {
public:
    template <typename T>
    Joiner& operator()(const T& v) {
        _out += format(FMT_STRING("{}{}"), _sep, stringifyForAssert(v));
        _sep = ", ";
        return *this;
    }
    explicit operator std::string() const {
        return _out;
    }

private:
    std::string _out;
    const char* _sep = "";
};

template <typename T>
std::string doSequence(const T& seq) {
    std::string r;
    Joiner joiner;
    for (const auto& e : seq)
        joiner(e);
    return format(FMT_STRING("[{}]"), std::string{joiner});
}

template <typename XTuple, typename MTuple, size_t... Is>
std::array<MatchResult, sizeof...(Is)> matchTuple(const XTuple& x,
                                                  const MTuple& ms,
                                                  std::index_sequence<Is...>) {
    return std::array{{std::get<Is>(ms).match(x)...}};
}

template <typename MTuple, size_t... Is>
std::string describeTuple(const MTuple& ms, std::index_sequence<Is...>) {
    detail::Joiner joiner;
    for (const auto& d : std::array{std::get<Is>(ms).describe()...})
        joiner(d);
    return std::string(joiner);
}

template <typename MTuple>
std::string describeTuple(const MTuple& ms) {
    return describeTuple(ms, std::make_index_sequence<std::tuple_size_v<MTuple>>{});
}

template <typename MTuple, size_t N, size_t... Is>
std::string matchTupleMessage(const MTuple& ms,
                              const std::array<MatchResult, N>& arr,
                              std::index_sequence<Is...>) {
    detail::Joiner joiner;
    (
        [&] {
            auto&& ri = arr[Is];
            if (!ri)
                joiner(format(FMT_STRING("{}:({}{}{})"),
                              Is,
                              std::get<Is>(ms).describe(),
                              ri.message().empty() ? "" : ":",
                              ri.message()));
        }(),
        ...);
    return format(FMT_STRING("failed: [{}]"), std::string{joiner});
}
template <typename MTuple, size_t N>
std::string matchTupleMessage(const MTuple& ms, const std::array<MatchResult, N>& arr) {
    return matchTupleMessage(ms, arr, std::make_index_sequence<std::tuple_size_v<MTuple>>{});
}

template <typename T>
std::string stringifyForAssert(const T& x) {
    if constexpr (HasOstream<T>) {
        return doOstream(x);
    } else if constexpr (HasToString<T>) {
        return x.toString();
    } else if constexpr (std::is_convertible_v<T, StringData>) {
        return doFormat(StringData(x));
    } else if constexpr (std::is_pointer_v<T>) {
        return doFormat(static_cast<const void*>(x));
    } else if constexpr (IsSequence<T>) {
        return doSequence(x);
    } else {
        return format(FMT_STRING("[{}@{}]"), demangleName(typeid(T)), (void*)&x);
    }
}

template <typename Tup>
auto onFailure(const Tup& tup, const char* expr) {
    const auto& [e, m, r] = tup;
    return format(FMT_STRING("value: {}, actual: {}{}, expected: {}"),
                  expr,
                  stringifyForAssert(e),
                  r.message().empty() ? "" : format(FMT_STRING(", {}"), r.message()),
                  m.describe());
}

}  // namespace detail

template <typename T>
class TypedMatcher {
public:
    using value_type = T;

    template <typename M>
    TypedMatcher(const M& m) : _m{std::make_shared<TypedMatch<M>>(m)} {}

    std::string describe() const {
        return _m->describe();
    }

    MatchResult match(const T& v) const {
        return _m->match(v);
    }

private:
    struct TypedMatchInterface {
        virtual std::string describe() const = 0;
        virtual MatchResult match(const T& v) const = 0;
    };
    template <typename M>
    class TypedMatch : public TypedMatchInterface {
        template <typename X>
        using CanMatchOp = decltype(std::declval<M>().match(std::declval<X>()));

    public:
        TypedMatch(const M& m) : _m{&m} {}
        std::string describe() const override {
            return _m->describe();
        }

        MatchResult match(const T& v) const override {
            if constexpr (!detail::IsDetectedV<CanMatchOp, T>) {
                return MatchResult{
                    false,
                    format(FMT_STRING("Matcher does not accept {}"), demangleName(typeid(T)))};
            } else {
                return _m->match(v);
            }
        }

    private:
        const M* _m;
    };
    std::shared_ptr<TypedMatchInterface> _m;
};

/**
 * MatchResult will be false when `m.match(v)` is ill-formed. Relaxes type safety for
 * statically polymorphic situations like variant or BSON value matching.
 */
template <typename M, typename T>
MatchResult matchAnyType(const M& m, const T& v) {
    return TypedMatcher<T>(m).match(v);
}

/** Always true: matches anything of any type */
class Any : public Matcher {
public:
    std::string describe() const {
        return "Any";
    }
    template <typename X>
    MatchResult match(const X&) const {
        return MatchResult{true};
    }
};

enum class RelOpId { kEq, kNe, kLt, kLe, kGt, kGe };

template <RelOpId op, typename T>
class RelOpBase : public Matcher {
    static constexpr StringData matcherName() {
        if constexpr (op == RelOpId::kEq) {
            return "Eq"_sd;
        } else if constexpr (op == RelOpId::kNe) {
            return "Ne"_sd;
        } else if constexpr (op == RelOpId::kLt) {
            return "Lt"_sd;
        } else if constexpr (op == RelOpId::kLe) {
            return "Le"_sd;
        } else if constexpr (op == RelOpId::kGt) {
            return "Gt"_sd;
        } else if constexpr (op == RelOpId::kGe) {
            return "Ge"_sd;
        }
    }

    static constexpr auto comparator() {
        if constexpr (op == RelOpId::kEq) {
            return std::equal_to<>{};
        } else if constexpr (op == RelOpId::kNe) {
            return std::not_equal_to<>{};
        } else if constexpr (op == RelOpId::kLt) {
            return std::less<>{};
        } else if constexpr (op == RelOpId::kLe) {
            return std::less_equal<>{};
        } else if constexpr (op == RelOpId::kGt) {
            return std::greater<>{};
        } else if constexpr (op == RelOpId::kGe) {
            return std::greater_equal<>{};
        }
    }

    template <typename X>
    using CanMatchOp =
        decltype(std::declval<decltype(comparator())>()(std::declval<X>(), std::declval<T>()));

public:
    explicit RelOpBase(T v) : _v{std::move(v)} {}

    std::string describe() const {
        return format(FMT_STRING("{}({})"), matcherName(), detail::stringifyForAssert(_v));
    }

    template <typename X, std::enable_if_t<detail::IsDetectedV<CanMatchOp, X>, int> = 0>
    MatchResult match(const X& x) const {
        return MatchResult{comparator()(x, _v)};
    }

private:
    T _v;
};

template <typename T>
struct Eq : RelOpBase<RelOpId::kEq, T> {
    Eq(T v) : RelOpBase<RelOpId::kEq, T>::RelOpBase(std::move(v)) {}
};
template <typename T>
struct Ne : RelOpBase<RelOpId::kNe, T> {
    Ne(T v) : RelOpBase<RelOpId::kNe, T>::RelOpBase(std::move(v)) {}
};
template <typename T>
struct Lt : RelOpBase<RelOpId::kLt, T> {
    Lt(T v) : RelOpBase<RelOpId::kLt, T>::RelOpBase(std::move(v)) {}
};
template <typename T>
struct Gt : RelOpBase<RelOpId::kGt, T> {
    Gt(T v) : RelOpBase<RelOpId::kGt, T>::RelOpBase(std::move(v)) {}
};
template <typename T>
struct Le : RelOpBase<RelOpId::kLe, T> {
    Le(T v) : RelOpBase<RelOpId::kLe, T>::RelOpBase(std::move(v)) {}
};
template <typename T>
struct Ge : RelOpBase<RelOpId::kGe, T> {
    Ge(T v) : RelOpBase<RelOpId::kGe, T>::RelOpBase(std::move(v)) {}
};

template <typename M>
class Not : public Matcher {
public:
    Not(M m) : _m(std::move(m)) {}

    std::string describe() const {
        return format(FMT_STRING("Not({})"), _m.describe());
    }

    template <typename X>
    MatchResult match(X&& x) const {
        auto r = _m.match(x);
        return MatchResult{!r};
    }

private:
    M _m;
};

template <typename... Ms>
class AllOf : public Matcher {
public:
    AllOf(Ms... ms) : _ms(std::move(ms)...) {}

    std::string describe() const {
        return format(FMT_STRING("AllOf({})"), detail::describeTuple(_ms));
    }

    template <typename X>
    MatchResult match(const X& x) const {
        return _match(x, IdxSeq{});
    }

private:
    template <typename X, size_t... Is>
    MatchResult _match(const X& x, std::index_sequence<Is...>) const {
        std::array arr{std::get<Is>(_ms).match(x)...};
        if (!std::all_of(arr.begin(), arr.end(), [](auto&& re) { return !!re; }))
            return MatchResult{false, detail::matchTupleMessage(_ms, arr)};
        return MatchResult{true};
    }

    using IdxSeq = std::index_sequence_for<Ms...>;
    std::tuple<Ms...> _ms;
};

template <typename... Ms>
class AnyOf : public Matcher {
public:
    AnyOf(Ms... ms) : _ms(std::move(ms)...) {}

    std::string describe() const {
        return format(FMT_STRING("AnyOf({})"), detail::describeTuple(_ms));
    }

    template <typename X>
    MatchResult match(const X& x) const {
        return _match(x, IdxSeq{});
    }

private:
    template <typename X, size_t... Is>
    MatchResult _match(const X& x, std::index_sequence<Is...>) const {
        std::array arr{std::get<Is>(_ms).match(x)...};
        if (!std::any_of(arr.begin(), arr.end(), [](auto&& re) { return !!re; }))
            return MatchResult{false, detail::matchTupleMessage(_ms, arr)};
        return MatchResult{true};
    }

    using IdxSeq = std::index_sequence_for<Ms...>;
    std::tuple<Ms...> _ms;
};

template <typename M>
class Pointee : public Matcher {
public:
    explicit Pointee(M m) : _m(std::move(m)) {}

    std::string describe() const {
        return format(FMT_STRING("Pointee({})"), _m.describe());
    }

    template <typename X>
    MatchResult match(const X& x) const {
        if (!x)
            return MatchResult{false, "empty pointer"};
        MatchResult res = _m.match(*x);
        if (res)
            return MatchResult{true};
        return MatchResult{false, format(FMT_STRING("{}"), res.message())};
    }

private:
    M _m;
};

class IsNull : public Matcher {
public:
    IsNull() = default;

    std::string describe() const {
        return "IsNull";
    }

    template <typename X>
    MatchResult match(const X& x) const {
        return MatchResult{!x};
    }
};

class ContainsRegex : public Matcher {
public:
    explicit ContainsRegex(std::string pattern);
    ~ContainsRegex();

    std::string describe() const;

    // Should accept anything string-like
    MatchResult match(StringData x) const {
        return _match(x.rawData(), x.size());
    }

private:
    class Impl;
    MatchResult _match(const char* ptr, size_t len) const;
    std::shared_ptr<const Impl> _impl;
};


template <typename... Ms>
class ElementsAre : public Matcher {
public:
    ElementsAre(const Ms&... ms) : _ms(std::move(ms)...) {}

    std::string describe() const {
        return format(FMT_STRING("ElementsAre({})"), detail::describeTuple(_ms));
    }

    template <typename X>
    MatchResult match(X&& x) const {
        return _match(x, IdxSeq{});
    }

private:
    template <typename X, size_t... Is>
    MatchResult _match(const X& x, std::index_sequence<Is...>) const {
        if (x.size() != kSize) {
            return MatchResult{
                false, format(FMT_STRING("failed: size {} != expected size {}"), x.size(), kSize)};
        }
        using std::begin;
        using std::end;
        auto it = begin(x);
        auto itEnd = end(x);
        std::array arr{std::get<Is>(_ms).match(*it++)...};
        bool allOk = true;
        detail::Joiner joiner;
        for (size_t i = 0; i != kSize; ++i) {
            if (!arr[i]) {
                allOk = false;
                std::string m;
                if (!arr[i].message().empty())
                    m = format(FMT_STRING(":{}"), arr[i].message());
                joiner(format(FMT_STRING("{}{}"), i, m));
            }
        }
        if (!allOk)
            return MatchResult{false, format(FMT_STRING("failed: [{}]"), std::string{joiner})};
        return MatchResult{true};
    }

    static constexpr size_t kSize = sizeof...(Ms);
    using IdxSeq = std::make_index_sequence<kSize>;
    std::tuple<Ms...> _ms;
};

template <typename... Ms>
class TupleElementsAre : public Matcher {
public:
    TupleElementsAre(const Ms&... ms) : _ms(std::move(ms)...) {}

    std::string describe() const {
        return format(FMT_STRING("TupleElementsAre({})"), detail::describeTuple(_ms));
    }

    template <typename X>
    MatchResult match(X&& x) const {
        return _match(x, IdxSeq{});
    }

private:
    template <typename X, size_t... Is>
    MatchResult _match(const X& x, std::index_sequence<Is...>) const {
        size_t xSize = std::tuple_size_v<X>;
        if (xSize != kSize)
            return MatchResult{
                false, format(FMT_STRING("failed: size {} != expected size {}"), xSize, kSize)};
        std::array arr{std::get<Is>(_ms).match(std::get<Is>(x))...};
        if (!std::all_of(arr.begin(), arr.end(), [](auto&& r) { return !!r; }))
            return MatchResult{false, detail::matchTupleMessage(_ms, arr)};
        return MatchResult{true};
    }

    static constexpr size_t kSize = sizeof...(Ms);
    using IdxSeq = std::make_index_sequence<kSize>;
    std::tuple<Ms...> _ms;
};

template <typename... Ms>
class StructuredBindingsAre : public Matcher {
public:
    StructuredBindingsAre(const Ms&... ms) : _ms(std::move(ms)...) {}

    std::string describe() const {
        return format(FMT_STRING("StructuredBindingsAre({})"), detail::describeTuple(_ms));
    }

    template <typename X>
    MatchResult match(const X& x) const {
        return _match(x, IdxSeq{});
    }

private:
    template <typename X, size_t... Is>
    MatchResult _match(const X& x, std::index_sequence<Is...>) const {
        auto tied = detail::tieStruct<kSize>(x);
        std::array arr{std::get<Is>(_ms).match(std::get<Is>(tied))...};
        if (!std::all_of(arr.begin(), arr.end(), [](auto&& r) { return !!r; }))
            return MatchResult{false, detail::matchTupleMessage(_ms, arr)};
        return MatchResult{true};
    }

private:
    static constexpr size_t kSize = sizeof...(Ms);
    using IdxSeq = std::make_index_sequence<kSize>;
    std::tuple<Ms...> _ms;
};

template <typename CodeM, typename ReasonM>
class StatusIs : public Matcher {
public:
    StatusIs(CodeM code, ReasonM reason) : _code{std::move(code)}, _reason{std::move(reason)} {}
    std::string describe() const {
        return format(FMT_STRING("StatusIs({}, {})"), _code.describe(), _reason.describe());
    }
    MatchResult match(const Status& st) const {
        MatchResult cr = _code.match(st.code());
        MatchResult rr = _reason.match(st.reason());
        detail::Joiner joiner;
        if (!cr.message().empty())
            joiner(format(FMT_STRING("code:{}"), cr.message()));
        if (!rr.message().empty()) {
            joiner(format(FMT_STRING("reason:{}"), rr.message()));
        }
        return MatchResult{cr && rr, std::string{joiner}};
    }

private:
    CodeM _code;
    ReasonM _reason;
};

template <typename M>
class BSONObjHas : public Matcher {
public:
    BSONObjHas(M m) : _m{std::move(m)} {}

    std::string describe() const {
        return format(FMT_STRING("BSONObjHas({})"), _m.describe());
    }

    MatchResult match(const BSONObj& x) const {
        std::vector<MatchResult> res;
        for (const auto& e : x) {
            if (auto mr = _m.match(e))
                return mr;
            else
                res.push_back(mr);
        }
        return MatchResult{false, "None of the elements matched"};
    }

private:
    M _m;
};

template <typename NameM, typename TypeM, typename ValueM>
class BSONElementIs : public Matcher {
public:
    BSONElementIs(NameM nameM, TypeM typeM, ValueM valueM)
        : _name{std::move(nameM)}, _type{std::move(typeM)}, _value{std::move(valueM)} {}

    std::string describe() const {
        return format(FMT_STRING("BSONElementIs(name:{}, type:{}, value:{})"),
                      _name.describe(),
                      _type.describe(),
                      _value.describe());
    }

    MatchResult match(const BSONElement& x) const {
        auto nr = _name.match(std::string{x.fieldNameStringData()});
        if (!nr)
            return MatchResult{
                false,
                format(FMT_STRING("name failed: {} {}"), x.fieldNameStringData(), nr.message())};
        auto t = x.type();
        auto tr = _type.match(t);
        if (!tr)
            return MatchResult{
                false, format(FMT_STRING("type failed: {} {}"), typeName(x.type()), tr.message())};
        if (t == NumberInt) {
            return matchAnyType(_value, x.Int());
        } else if (t == NumberLong) {
            return matchAnyType(_value, x.Long());
        } else if (t == NumberDouble) {
            return matchAnyType(_value, x.Double());
        } else if (t == String) {
            return matchAnyType(_value, x.String());
        }
        // need to support more of the BSONType's ...
        return MatchResult{false};
    }

private:
    NameM _name;
    TypeM _type;
    ValueM _value;
};
// template <BSONType bt, typename M>
// auto BSONElementIs(std::string name, M m) {
//    return BSONElementIs_<bt, M>{std::move(name), std::move(m)};
//}

inline std::string stringifyForAssert(ErrorCodes::Error ec) {
    return ErrorCodes::errorString(ec);
}

}  // namespace mongo::unittest::match

#define ASSERT_THAT(expr, matcher)                                                      \
    if (auto tup_ = std::tuple{expr, matcher, ::mongo::unittest::match::MatchResult{}}; \
        std::get<2>(tup_) = std::get<1>(tup_).match(std::get<0>(tup_))) {               \
    } else                                                                              \
        FAIL(::mongo::unittest::match::detail::onFailure(tup_, #expr))
