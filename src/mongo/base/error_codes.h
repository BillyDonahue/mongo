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
#include <iosfwd>
#include <string>

#include "mongo/base/error_codes_tables.h"
#include "mongo/base/string_data.h"
#include "mongo/platform/compiler.h"

namespace mongo {

class Status;
class DBException;


enum class ErrorCategory {
#define onCategory(catName) catName
ERROR_CODES_FOR_EACH_CATEGORY(onCategory,ERROR_CODES_COMMA)
#undef onCategory
};

/**
 * Class containing a table of error codes and their corresponding error strings.
 * The tables expanded here are derived from the definitions in
 * src/mongo/base/error_codes.err file and the
 * src/mongo/base/error_codes_table.tpl.h template.
 */
class ErrorCodes {
public:
    // Explicitly 32-bits wide so that non-symbolic values,
    // like uassert codes, are valid.
    enum Error : std::int32_t {
#define onCode(ecName,ecCode) ecName = ecCode,
        ERROR_CODES_FOR_EACH_CODE(onCode,)
#undef onCode
        MaxError
    };

    static std::string errorString(Error err);

    /**
     * Parses an Error from its "name".  Returns UnknownError if "name" is unrecognized.
     *
     * NOTE: Also returns UnknownError for the string "UnknownError".
     */
    static Error fromString(StringData name);

    /**
     * Reuses a unique numeric code in a way that supresses the duplicate code detection. This
     * should only be used when testing error cases to ensure that the code under test fails in the
     * right place. It should NOT be used in non-test code to either make a new error site (use
     * ErrorCodes::Error(CODE) for that) or to see if a specific failure case occurred (use named
     * codes for that).
     */
    static Error duplicateCodeForTest(int code) {
        return static_cast<Error>(code);
    }

    /**
     * Generic predicate to test if a given error code is in a category.
     *
     * This version is intended to simplify forwarding by Status and DBException. Non-generic
     * callers should just use the specific isCategoryName() methods instead.
     */
    template <ErrorCategory category>
    static bool isA(Error err);

    template <ErrorCategory category, typename C>
    static bool isA(const C& errorContainer) {
        return isA<category>(errorContainer.code());
    }

#define onCategory(cat) \
    static bool is##cat(Error code) { return isA<ErrorCategory::cat>(code); }
    ERROR_CODES_FOR_EACH_CATEGORY(onCategory,)
#undef onCategory

#define onCategory(cat) \
    template <typename C> \
    static bool is##cat(const C& errorContainer) { \
        return isA<ErrorCategory::cat>(errorContainer.code()); \
    }
    ERROR_CODES_FOR_EACH_CATEGORY(onCategory,)
#undef onCategory

    static bool shouldHaveExtraInfo(Error code);
};

inline std::ostream& operator<<(std::ostream& stream, ErrorCodes::Error code) {
    return stream << ErrorCodes::errorString(code);
}

/**
 * This namespace contains implementation details for our error handling code and should not be used
 * directly in general code.
 */
namespace error_details {

//
// Traits
//

template <typename E, E... elems> struct EnumSequence {};
template <typename E, E v> struct EnumConstant { static constexpr E value = v; };

template <auto x, typename E, E... elems>
constexpr bool Contains(EnumSequence<E, elems...>) {
    return ((x == elems) || ... );
}

template <ErrorCategory... categories>
using CategoryList = EnumSequence<ErrorCategory, categories...>;
template <ErrorCodes::Error... codes>
using CodeList = EnumSequence<ErrorCodes::Error, codes...>;

template <ErrorCodes::Error err> using CodeTag = EnumConstant<ErrorCodes::Error, err>;
template <ErrorCategory cat> using CategoryTag = EnumConstant<ErrorCategory, cat>;

#define onCode(ecName, ecCode) ErrorCodes::ecName
using AllCodes = CodeList<ERROR_CODES_FOR_EACH_CODE(onCode, ERROR_CODES_COMMA)>;
#undef onCode

template <int32_t code>
constexpr bool isNamedCode = Contains<(ErrorCodes::Error)code>(AllCodes{});

MONGO_COMPILER_NORETURN void throwExceptionForStatus(const Status& status);

//
// ErrorCategoriesFor
//

template <ErrorCodes::Error code>
struct ErrorCategoriesForImpl {
    using type = CategoryList<>;
};

#define onCode(ecName, ecCategories) \
template <> \
struct ErrorCategoriesForImpl<ErrorCodes::ecName> { \
    using type = CategoryList< ERROR_CODES_UNPACK(ecCategories) >; \
};
#define onCodeCategory(ecName, catName) ErrorCategory::catName
ERROR_CODES_FOR_EACH_CODE_THEN_CATEGORY(onCode, onCodeCategory, ERROR_CODES_COMMA)
#undef onCode
#undef onCodeCategory

template <ErrorCodes::Error code>
using ErrorCategoriesFor = typename ErrorCategoriesForImpl<code>::type;

//
// ErrorExtraInfoFor
//

template <ErrorCodes::Error code>
struct ErrorExtraInfoForImpl {};

template <ErrorCodes::Error code>
using ErrorExtraInfoFor = typename ErrorExtraInfoForImpl<code>::type;

template <ErrorCodes::Error code, typename = void>
struct HasExtraInfo : std::false_type {};
template <ErrorCodes::Error code>
struct HasExtraInfo<code, std::void_t<ErrorExtraInfoFor<code>>> : std::true_type {};


template <typename F, typename ...A>
bool staticSwitch(ErrorCodes::Error err, F&& f, A&&... as) {
    auto fwd = [&](auto tag) {
        std::forward<F>(f)(tag, std::forward<A>(as)...);
    };
    switch (err) {
#define onCode(ecName, ecCode) \
        case ErrorCodes::ecName: fwd(CodeTag<ErrorCodes::ecName>{}); return true;
ERROR_CODES_FOR_EACH_CODE(onCode,)
#undef onCode
        default: return false;
    }
}

}  // namespace error_details

template <ErrorCategory category>
bool ErrorCodes::isA(Error err) {
    bool r = false;
    error_details::staticSwitch(err, [&](auto tag) {
        r = error_details::Contains<category>(error_details::ErrorCategoriesFor<tag.value>{});
    });
    return r;
}

// Declare ErrorExtraInfo subclasses.
#define onExtraClass(ecName, ecExtraClass) class ecExtraClass;
#define onExtraClassNs(ecName, ecExtraClass, ecExtraNs) namespace ecExtraNs { class ecExtraClass; }
ERROR_CODES_FOR_EACH_EXTRA_INFO(onExtraClass, onExtraClassNs)
#undef onExtraClass
#undef onExtraClassNs

#define onExtraClass(ecName, ecExtraClass) \
template <> \
struct error_details::ErrorExtraInfoForImpl<ErrorCodes::ecName> { \
    using type = ecExtraClass; \
};
#define onExtraClassNs(ecName, ecExtraClass, ecExtraNs) \
    onExtraClass(ecName, ecExtraNs::ecExtraClass)
ERROR_CODES_FOR_EACH_EXTRA_INFO(onExtraClass, onExtraClassNs)
#undef onExtraClass
#undef onExtraClassNs

}  // namespace mongo
