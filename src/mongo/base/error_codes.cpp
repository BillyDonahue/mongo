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

#include "mongo/base/error_codes.h"

#include "mongo/base/static_assert.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/str.h"

namespace mongo {

namespace {

template <ErrorCodes::Error> ErrorExtraInfo::Parser* parser;
template <ErrorCodes::Error> constexpr StringData errorName;

#define onCode(ecName, ecCode) \
template<> constexpr StringData errorName<ErrorCodes::ecName> = #ecName ## _sd;
ERROR_CODES_FOR_EACH_CODE(onCode,)
#undef onCode

}  // namespace


MONGO_STATIC_ASSERT(sizeof(ErrorCodes::Error) == sizeof(int));

std::string ErrorCodes::errorString(Error err) {
    if (StringData r; error_details::staticSwitch(err, [&](auto tag) {
            r = errorName<tag.value>;
        })) {
        return std::string(r);
    }
    return str::stream() << "Location" << std::underlying_type_t<Error>(err);
}

static ErrorCodes::Error fromStringImpl(StringData name, error_details::CodeList<>) {
    return ErrorCodes::UnknownError;
}

template <ErrorCodes::Error c0, ErrorCodes::Error... codes>
static ErrorCodes::Error fromStringImpl(StringData name, error_details::CodeList<c0, codes...>) {
    if (name == errorName<c0>) return c0;
    return fromStringImpl(name, error_details::CodeList<codes...>{});
}

ErrorCodes::Error ErrorCodes::fromString(StringData name) {
    return fromStringImpl(name, error_details::AllCodes{});
}

bool ErrorCodes::shouldHaveExtraInfo(Error code) {
    bool r = false;
    error_details::staticSwitch(code, [&](auto tag) {
        r = error_details::HasExtraInfo<tag.value>{};
    });
    return r;
}

ErrorExtraInfo::Parser** parserPtr(ErrorCodes::Error code) {
    ErrorExtraInfo::Parser** p = nullptr;
    error_details::staticSwitch(code, [&](auto tag) {
        if constexpr (error_details::HasExtraInfo<tag.value>{}) {
            p = &parser<tag.value>;
        }
    });
    return p;
}

ErrorExtraInfo::Parser* ErrorExtraInfo::parserFor(ErrorCodes::Error code) {
    auto p = parserPtr(code);
    if (!p) return nullptr;
    invariant(*p);
    return *p;
}

void ErrorExtraInfo::registerParser(ErrorCodes::Error code, Parser* parser) {
    auto p = parserPtr(code);
    invariant(p);
    invariant(!*p);
    *p = parser;
}

template <ErrorCodes::Error... codes>
static void invariantHaveParser(error_details::CodeList<codes...>) {
    auto f = [](auto tag) {
        if constexpr (error_details::HasExtraInfo<tag.value>{}) {
            invariant(parser<tag.value>);
        }
        return true;
    };
    ( f(error_details::CodeTag<codes>{}), ... );
}

void ErrorExtraInfo::invariantHaveAllParsers() {
    invariantHaveParser(error_details::AllCodes{});
}

void error_details::throwExceptionForStatus(const Status& status) {
    staticSwitch(status.code(), [&](auto&& tag) { throw ExceptionFor<tag.value>(status); });
    /**
     * This type is used for all exceptions that don't have a more specific type. It is defined
     * locally in this function to prevent anyone from catching it specifically separately from
     * AssertionException.
     */
    class NonspecificAssertionException final : public AssertionException {
    public:
        using AssertionException::AssertionException;

    private:
        void defineOnlyInFinalSubclassToPreventSlicing() final {}
    };
    throw NonspecificAssertionException(status);
}

}  // namespace mongo
