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

/**
 * Unit tests of the InitializerDependencyGraph type.
 * DependencyGraph has its own test where the harder graph testing is.
 * This is only testing the initializer-related behavior.
 */

#include <algorithm>
#include <string>
#include <vector>

#include <fmt/ranges.h>

#include "mongo/base/init.h"
#include "mongo/base/initializer_dependency_graph.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

template <typename C, typename T>
size_t count(const C& c, const T& value) {
    return std::count(c.begin(), c.end(), value);
}

void doNothing(InitializerContext*) {}

TEST(InitializerDependencyGraphTest, InsertNullFunctionFails) {
    InitializerDependencyGraph graph;
    ASSERT_THROWS_CODE(
        graph.addInitializer("A", nullptr, nullptr, {}, {}), DBException, ErrorCodes::BadValue);
}

TEST(InitializerDependencyGraphTest, FreezeCausesFrozen) {
    InitializerDependencyGraph graph;
    ASSERT_FALSE(graph.frozen());
    graph.freeze();
    ASSERT_TRUE(graph.frozen());
    graph.freeze();
    ASSERT_TRUE(graph.frozen());
}

TEST(InitializerDependencyGraphTest, TopSortEmptyGraphWhileFrozen) {
    InitializerDependencyGraph graph;
    graph.freeze();
    auto nodeNames = graph.topSort();
    ASSERT_EQ(nodeNames.size(), 0);
}

TEST(InitializerDependencyGraphTest, TopSortGraphNoDepsWhileFrozen) {
    InitializerDependencyGraph graph;
    graph.addInitializer("A", doNothing, nullptr, {}, {});
    graph.addInitializer("B", doNothing, nullptr, {}, {});
    graph.addInitializer("C", doNothing, nullptr, {}, {});
    graph.freeze();
    auto nodeNames = graph.topSort();
    ASSERT_EQ(nodeNames.size(), 3);
    ASSERT_EQ(count(nodeNames, "A"), 1);
    ASSERT_EQ(count(nodeNames, "B"), 1);
    ASSERT_EQ(count(nodeNames, "C"), 1);
}

TEST(InitializerDependencyGraphTest, CannotAddWhenFrozen) {
    InitializerDependencyGraph graph;
    graph.freeze();
    ASSERT_THROWS_CODE(graph.addInitializer("A", doNothing, nullptr, {}, {}),
                       DBException,
                       ErrorCodes::CannotMutateObject);
}

}  // namespace
}  // namespace mongo
