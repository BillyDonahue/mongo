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

#include "mongo/bson/json.h"
#include "mongo/db/commands/parameters.h"
#include "mongo/db/commands/parameters_gen.h"
#include "mongo/db/jsobj.h"
#include "mongo/logv2/log_component.h"
#include "mongo/logv2/log_severity.h"
#include "mongo/unittest/unittest.h"

namespace mongo::server_parameter_detail {
namespace {
bool eq(LogComponentSetting a, LogComponentSetting b) {
    auto t = [](auto&& x) { return std::tie(x.component, x.level); };
    return t(a) == t(b);
}

std::ostream& print(std::ostream& os, LogComponentSetting x) {
    return os << "{" << x.component.getShortName() << "," << x.level << "}";
}

template <typename Vec>
std::ostream& printVec(std::ostream& os, const Vec& vec) {
    os << "[";
    const char* sep = "";
    for (auto&& e : vec) {
        os << sep;
        sep = ", ";
        print(os, e);
    }
    os << "]";
    return os;
}

template <typename Vec>
void assertVec(const Vec& actual, const Vec& expected) {
    auto [it1, it2] =
        std::mismatch(actual.begin(), actual.end(), expected.begin(), expected.end(), eq);
    if (it1 == actual.end() && it2 == expected.end())
        return;
    std::ostringstream ss;
    printVec(ss, actual);
    ss << "!=";
    printVec(ss, expected);
    FAIL(ss.str());
}

class ParseLogComponentSettings : public unittest::Test {
public:
    using LogComponent = logv2::LogComponent;
    using unittest::Test::Test;

    std::vector<LogComponentSetting> doParse(const std::string& json) {
        return _parseLogComponentSettings(fromjson(json));
    }

    void assertParse(const std::string& json, const std::vector<LogComponentSetting>& expected) {
        assertVec(doParse(json), expected);
    }

    void assertThrows(const std::string& json, ErrorCodes::Error err, const std::string& reason) {
        auto checker = [=](const DBException& ex) {
            if (ex.code() != err)
                return false;
            if (ex.reason() != reason)
                return false;
            return true;
        };
        ASSERT_THROWS_WITH_CHECK(doParse(json), DBException, checker);
    }
};

TEST_F(ParseLogComponentSettings, Empty) {
    assertParse(R"({})", {});
}

TEST_F(ParseLogComponentSettings, Numeric) {
    assertParse(R"({"verbosity": 1}})", {{LogComponent::kDefault, 1}});
}

TEST_F(ParseLogComponentSettings, FailNonNumeric) {
    assertThrows(R"({"verbosity: "not a number"})",
                 ErrorCodes::BadValue,
                 "Expected default.verbosity to be a number, but found string");
}

TEST_F(ParseLogComponentSettings, FailBadComponent) {
    assertThrows(R"({"NoSuchComponent": 2})",
                 ErrorCodes::BadValue,
                 "Invalid component name default.NoSuchComponent");
}

TEST_F(ParseLogComponentSettings, NestedNumeric) {
    assertParse(R"({"accessControl": {"verbosity": 1}})", {{LogComponent::kAccessControl, 1}});
}

TEST_F(ParseLogComponentSettings, NestedNonNumeric) {
    assertThrows(R"({"accessControl": {"verbosity": "Not a number"}})",
                 ErrorCodes::BadValue,
                 "Expected accessControl.verbosity to be a number, but found string");
}

TEST_F(ParseLogComponentSettings, NestedBadComponent) {
    assertThrows(R"({"NoSuchComponent": {"verbosity": 2}})",
                 ErrorCodes::BadValue,
                 "Invalid component name default.NoSuchComponent");
}

TEST_F(ParseLogComponentSettings, MultiNumeric) {
    assertParse(
        R"({
            "verbosity":2,
            "accessControl":{
                "verbosity":0
            },
            "storage":{
                "verbosity":3,
                "journal":{
                    "verbosity":5
                }
            }
        })",
        {{LogComponent::kDefault, 2},
         {LogComponent::kAccessControl, 0},
         {LogComponent::kStorage, 3},
         {LogComponent::kJournal, 5}});
}

TEST_F(ParseLogComponentSettings, MultiFailBadComponent) {
    assertThrows(
        R"({
            "verbosity": 6,
            "accessControl": {
                "verbosity": 5
            },
            "storage": {
                "verbosity": 4,
                "journal": {
                    "verbosity": 6
                }
            },
            "No Such Component": {
                "verbosity": 2
            },
            "extrafield": 123
        })",
        ErrorCodes::BadValue,
        "Invalid component name default.No Such Component");
}

TEST_F(ParseLogComponentSettings, DeeplyNestedFailFast) {
    assertThrows(R"({"storage":{"this":{"is":{"nested":{"too":"deeply"}}}}})",
                 ErrorCodes::BadValue,
                 "Invalid component name storage.this");
}

TEST_F(ParseLogComponentSettings, DeeplyNestedFailLast) {
    assertThrows(R"({"storage":{"journal":{"No Such Component":"bad"}}})",
                 ErrorCodes::BadValue,
                 "Invalid component name storage.journal.No Such Component");
}

}  // namespace
}  // namespace mongo::server_parameter_detail
