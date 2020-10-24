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

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest
#include "mongo/platform/basic.h"

#include <functional>
#include <string>
#include <list>
#include <vector>

#include "mongo/base/status.h"
#include "mongo/logv2/log.h"
#include "mongo/unittest/assert_that.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"


namespace mongo::unittest::match {
namespace {

#define GET_FAILURE_STRING(v, m)                            \
    [&] {                                                   \
        try {                                               \
            ASSERT_THAT(v, m);                              \
            return std::string{};                           \
        } catch (const TestAssertionFailureException& ex) { \
            return ex.what();                               \
        }                                                   \
    }()

TEST(AssertThat, AssertThat) {
    ASSERT_THAT(123, Eq(123));
    ASSERT_THAT(0, Not(Eq(123)));
    ASSERT_THAT(std::string("hi"), Eq(std::string("hi")));
    ASSERT_THAT("hi", Not(Eq(std::string("Hi"))));
    ASSERT_THAT(123., Eq(123));
    int x = 456;
    auto failStr = GET_FAILURE_STRING(x + 1, Eq(123));
    ASSERT_EQ(failStr, "value: x + 1, actual: 457, expected: Eq(123)");
}

TEST(AssertThat, MatcherDescribe) {
    ASSERT_EQ(Eq(123).describe(), "Eq(123)");
    ASSERT_EQ(Not(Eq(123)).describe(), "Not(Eq(123))");
}

TEST(AssertThat, AllOf) {
    {
        auto m = AllOf(Eq(123), Not(Eq(0)));
        ASSERT_TRUE(m.match(123));
        ASSERT_EQ(m.describe(), "AllOf(Eq(123), Not(Eq(0)))");
        ASSERT_THAT(123, m);
    }
    {
        auto m = AllOf(Eq(1), Eq(2), Eq(3));
        ASSERT_FALSE(m.match(2));
        ASSERT_EQ(m.describe(), "AllOf(Eq(1), Eq(2), Eq(3))");
        ASSERT_EQ(m.match(2).message(), "failed: [0:(Eq(1)), 2:(Eq(3))]");
    }
}

TEST(AssertThat, AnyOf) {
    auto m = AnyOf(Eq(123), Not(Eq(4)));
    ASSERT_TRUE(m.match(123));
    ASSERT_EQ(m.describe(), "AnyOf(Eq(123), Not(Eq(4)))");
    ASSERT_THAT(123, m);
    ASSERT_FALSE(m.match(4));
    ASSERT_EQ(m.match(4).message(), "failed: [0:(Eq(123)), 1:(Not(Eq(4)))]");
}

TEST(AssertThat, IsNull) {
    int v1 = 123;
    int* np = nullptr;
    auto m = IsNull();
    ASSERT_EQ(m.describe(), "IsNull");
    ASSERT_TRUE(m.match(np));
    ASSERT_FALSE(m.match(&v1));
    ASSERT_THAT(np, m);
    ASSERT_EQ(m.match(&v1).message(), "");
    ASSERT_EQ(m.match(np).message(), "");
}

TEST(AssertThat, Pointee) {
    int v1 = 123;
    int v2 = 4;
    auto m = Pointee(Eq(123));
    ASSERT_EQ(m.describe(), "Pointee(Eq(123))");
    ASSERT_TRUE(m.match(&v1));
    ASSERT_FALSE(m.match(&v2));
    ASSERT_THAT(&v1, m);
    ASSERT_EQ(m.match(&v2).message(), "");
    ASSERT_EQ(m.match((int*)nullptr).message(), "empty pointer");
}

TEST(AssertThat, ContainsRegex) {
    auto m = ContainsRegex(R"re(aa*\d*)re");
    ASSERT_EQ(m.describe(), R"re(ContainsRegex("aa*\d*"))re");
    ASSERT_TRUE(m.match("aaa123"));
    ASSERT_FALSE(m.match("zzz"));
    ASSERT_THAT("aaa123", m);
    ASSERT_EQ(m.match("zzz").message(), "");
}

TEST(AssertThat, ElementsAre) {
    auto m = ElementsAre(Eq(111), Eq(222), Eq(333));
    ASSERT_EQ(m.describe(), "ElementsAre(Eq(111), Eq(222), Eq(333))");
    ASSERT_TRUE(m.match(std::vector<int>{111, 222, 333}));
    ASSERT_FALSE(m.match(std::vector<int>{111, 222, 333, 444}));
    ASSERT_FALSE(m.match(std::vector<int>{111, 222, 444}));
    ASSERT_FALSE(m.match(std::vector<int>{111, 222, 444}));
    {
        auto failStr = GET_FAILURE_STRING(std::vector<int>({111, 222, 444}), m);
        ASSERT_EQ(failStr,
                  "value: std::vector<int>({111, 222, 444})"
                  ", actual: [111, 222, 444]"
                  ", failed: [2]"
                  ", expected: ElementsAre(Eq(111), Eq(222), Eq(333))");
    }
    {
        auto failStr = GET_FAILURE_STRING(std::vector<int>({111, 222}), m);
        ASSERT_EQ(failStr,
                  "value: std::vector<int>({111, 222})"
                  ", actual: [111, 222]"
                  ", failed: size 2 != expected size 3"
                  ", expected: ElementsAre(Eq(111), Eq(222), Eq(333))");
    }
}

TEST(AssertThat, TupleElementsAre) {
    ASSERT_THAT((std::tuple{123, "hi"}), TupleElementsAre(Eq(123), Eq("hi")));
}

TEST(AssertThat, StructuredBindingsAre) {
    struct X {
        int i;
        std::string str;
    };
    ASSERT_THAT((X{123, "hi"}), StructuredBindingsAre(Eq(123), Eq("hi")));
#if 0  // Must not compile. Check manually I guess.
    struct MoreFields {int i1; int i2; };
    ASSERT_THAT((MoreFields{123, 456}), StructuredBindingsAre(Eq(123)));
#endif
}


TEST(AssertThat, StatusIs) {
    ASSERT_THAT(Status::OK(), StatusIs(Eq(ErrorCodes::OK), Eq("")));
    Status oops{ErrorCodes::InternalError, "oops I did it again"};
    ASSERT_THAT(oops, StatusIs(Eq(ErrorCodes::InternalError), Eq("oops I did it again")));
    ASSERT_THAT(oops, StatusIs(Ne(ErrorCodes::OK), Any()));
    ASSERT_THAT(oops, StatusIs(Ne(ErrorCodes::OK), ContainsRegex("o*ps")));
}

TEST(AssertThat, BSONObj) {
    auto obj = BSONObjBuilder{}.append("i", 123).append("s", "hi").obj();
    ASSERT_THAT(obj, BSONObjHas(BSONElementIs(Eq("i"), Eq(NumberInt), Any())));
    ASSERT_THAT(obj,
                AllOf(BSONObjHas(BSONElementIs(Eq("i"), Eq(NumberInt), Eq(123))),
                      BSONObjHas(BSONElementIs(Eq("s"), Eq(String), Eq("hi")))));
    ASSERT_THAT(obj, Not(BSONObjHas(BSONElementIs(Eq("x"), Any(), Any()))));
}


TEST(AssertThat, Demo) {
    ASSERT_THAT(123, Eq(123));
    ASSERT_THAT(123, Not(Eq(0)));
    ASSERT_THAT("hi", Eq("hi"));
    ASSERT_THAT("Four score and seven", AllOf(
                Ne("hi"),
                ContainsRegex("score"),
                ContainsRegex(R"( \w{5} )")));

    // Composing matchers
    ASSERT_THAT(123, Not(Eq(0)));
    ASSERT_THAT(123, AllOf(Gt(0), Lt(1000)));

    // Sequences
    std::vector<int> myVec{111, 222, 333};
    std::list<int> myList{111, 222, 333};
    ASSERT_THAT(myVec, Eq(std::vector<int>{111,222,333}));
    ASSERT_THAT(myVec, ElementsAre(Eq(111), AllOf(Lt(1000), Gt(0)), Any()));
    ASSERT_THAT(myList, ElementsAre(Eq(111), AllOf(Lt(1000), Gt(0)), Any()));

    // Structs/Tuples
    struct {int i; std::string s;} x{123, "hello"};
    ASSERT_THAT(x, StructuredBindingsAre(Eq(123), ContainsRegex("hel*o")));

    // Status
    Status oops{ErrorCodes::InternalError, "oops I did it again"};
    ASSERT_THAT(oops, StatusIs(Eq(ErrorCodes::InternalError), Eq("oops I did it again")));
    ASSERT_THAT(oops, StatusIs(Ne(ErrorCodes::OK), Any()));
    ASSERT_THAT(oops, StatusIs(Ne(ErrorCodes::OK), ContainsRegex("o*ps")));

    // BSONElement and BSONObj
    auto obj = BSONObjBuilder{}.append("i", 123).append("s", "hi").obj();
    ASSERT_THAT(obj,
                AllOf(BSONObjHas(BSONElementIs(Eq("i"), Eq(NumberInt), Eq(123))),
                      BSONObjHas(BSONElementIs(Eq("s"), Eq(String), Eq("hi")))));
    ASSERT_THAT(obj, Not(BSONObjHas(BSONElementIs(Eq("x"), Any(), Any()))));
}

}  // namespace
}  // namespace mongo::unittest::match
