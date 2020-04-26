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
 * Unit tests of the Initializer type.
 */

#include <fmt/format.h>

#include "mongo/base/init.h"
#include "mongo/base/initializer.h"
#include "mongo/base/initializer_dependency_graph.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

using namespace fmt::literals;

class InitializerTest : public unittest::Test {
public:
    enum State {
        kUnset = 0,
        kInitialized = 1,
        kDeinitialized = 2,
    };

    struct Graph {
        struct Node {
            std::string name;
            std::vector<size_t> prereqs;
        };

        /**
         * The dependency graph expressed as a vector of vectors.
         * Each row is a vector of the corresponding node's dependencies.
         */
        auto prerequisites() const {
            std::vector<std::vector<size_t>> result;
            for (const auto& node : nodes)
                result.push_back(node.prereqs);
            return result;
        }

        /** Invert the prereq edges. */
        auto dependents() const {
            std::vector<std::vector<size_t>> result(nodes.size());
            for (size_t i = 0; i != nodes.size(); ++i)
                for (auto& r : nodes[i].prereqs)
                    result[r].push_back(i);
            return result;
        }

        size_t size() const {
            return nodes.size();
        }

        std::vector<Node> nodes;
    };

    /** The arguments for an addInitializer call. */
    struct NodeSpec {
        std::string name;
        std::function<void(InitializerContext*)> init;
        std::function<void(DeinitializerContext*)> deinit;
        std::vector<std::string> prerequisites;
        std::vector<std::string> dependents;
    };

    void initImpl(size_t idx) {
        auto reqs = graph.prerequisites()[idx];
        for (auto req : reqs)
            if (states[req] != kInitialized)
                uasserted(ErrorCodes::UnknownError,
                          "(init{0}) {1} not already initialized"_format(idx, req));
        states[idx] = kInitialized;
    }

    void deinitImpl(size_t idx) {
        if (states[idx] != kInitialized)
            uasserted(ErrorCodes::UnknownError, "(deinit{0}) {0} not initialized"_format(idx));
        auto deps = graph.dependents()[idx];
        for (auto dep : deps)
            if (states[dep] != kDeinitialized)
                uasserted(ErrorCodes::UnknownError,
                          "(deinit{0}) {1} not already deinitialized"_format(idx, dep));
        states[idx] = kDeinitialized;
    }

    static void initNoop(InitializerContext*) {}
    static void deinitNoop(DeinitializerContext*) {}

    std::vector<NodeSpec> makeNormalDependencyGraphSpecs(const Graph& graph) {
        std::vector<NodeSpec> specs;
        for (size_t idx = 0; idx != graph.size(); ++idx) {
            std::vector<std::string> reqNames;
            for (auto&& req : graph.nodes[idx].prereqs)
                reqNames.push_back(graph.nodes[req].name);
            specs.push_back({graph.nodes[idx].name,
                             [this, idx](InitializerContext*) { initImpl(idx); },
                             [this, idx](DeinitializerContext*) { deinitImpl(idx); },
                             reqNames,
                             {}});
        }
        return specs;
    }

    void constructDependencyGraph(InitializerDependencyGraph& g,
                                  const std::vector<NodeSpec>& nodeSpecs) {
        for (const auto& n : nodeSpecs)
            ASSERT_OK(g.addInitializer(n.name, n.init, n.deinit, n.prerequisites, n.dependents))
                << n.name;
    }

    void constructNormalDependencyGraph(InitializerDependencyGraph& initializer) {
        constructDependencyGraph(initializer, makeNormalDependencyGraphSpecs(graph));
    }

    /*
     * Unless otherwise specified, all tests herein use the following
     * dependency graph.
     *
     * 0 <-  3 <- 7
     *  ^   / ^    ^
     *   \ v   \     \
     *    2     5 <-  8
     *   / ^   /     /
     *  v   \ v    v
     * 1 <-  4 <- 6
     */
    const Graph graph{{
        {"n0", {}},
        {"n1", {}},
        {"n2", {0, 1}},
        {"n3", {0, 2}},
        {"n4", {1, 2}},
        {"n5", {3, 4}},
        {"n6", {4}},
        {"n7", {3}},
        {"n8", {5, 6, 7}},
    }};

    std::vector<State> states = std::vector<State>(graph.size(), kUnset);
};

TEST_F(InitializerTest, SuccessfulInitializationAndDeinitialization) {
    Initializer initializer;
    constructNormalDependencyGraph(initializer.getInitializerDependencyGraph());

    ASSERT_OK(initializer.executeInitializers({}));
    for (size_t i = 0; i != states.size(); ++i)
        ASSERT_EQ(states[i], kInitialized) << i;

    ASSERT_OK(initializer.executeDeinitializers());
    for (size_t i = 0; i != states.size(); ++i)
        ASSERT_EQ(states[i], kDeinitialized) << i;
}

TEST_F(InitializerTest, Init5Misimplemented) {
    auto specs = makeNormalDependencyGraphSpecs(graph);
    for (auto&& spec : specs)
        spec.deinit = deinitNoop;
    specs[5].init = initNoop;
    Initializer initializer;
    constructDependencyGraph(initializer.getInitializerDependencyGraph(), specs);

    ASSERT_EQ(initializer.executeInitializers({}), ErrorCodes::UnknownError);

    std::vector<State> expected{
        kInitialized,
        kInitialized,
        kInitialized,
        kInitialized,
        kInitialized,
        kUnset,  // 5: noop init
        kInitialized,
        kInitialized,
        kUnset,  // 8: depends on states[5] == kIninitialized, so fails.
    };
    for (size_t i = 0; i != states.size(); ++i)
        ASSERT_EQ(states[i], expected[i]) << i;
}

TEST_F(InitializerTest, Deinit2Misimplemented) {
    auto specs = makeNormalDependencyGraphSpecs(graph);
    specs[2].deinit = deinitNoop;
    Initializer initializer;
    constructDependencyGraph(initializer.getInitializerDependencyGraph(), specs);

    ASSERT_OK(initializer.executeInitializers({}));
    for (size_t i = 0; i != states.size(); ++i)
        ASSERT_EQ(states[i], kInitialized) << i;

    ASSERT_EQ(initializer.executeDeinitializers(), ErrorCodes::UnknownError);

    // Since [2]'s deinit has been replaced with deinitNoop, it does not set states[2]
    // to kDeinitialized. Its dependents [0] and [1] will check for this and fail
    // with UnknownError, also remaining in the kInitialized state themselves.
    std::vector<State> expected{
        kInitialized, // 0: depends on states[2] == kDeinitialized, so fails
        kInitialized, // 1: depends on states[2] == kDeinitialized, so fails
        kInitialized, // 2: noop deinit
        kDeinitialized,
        kDeinitialized,
        kDeinitialized,
        kDeinitialized,
        kDeinitialized,
        kDeinitialized,
    };
    for (size_t i = 0; i != states.size(); ++i)
        ASSERT_EQ(states[i], expected[i]) << i;
}

DEATH_TEST_F(InitializerTest, CannotAddInitializerAfterInitializing, "!frozen()") {
    Initializer initializer;
    constructNormalDependencyGraph(initializer.getInitializerDependencyGraph());

    ASSERT_OK(initializer.executeInitializers({}));

    ASSERT_OK(initializer.getInitializerDependencyGraph().addInitializer(
        "test", initNoop, deinitNoop, {}, {}));
}

DEATH_TEST_F(InitializerTest, CannotDoubleInitialize, "invalid initializer state transition") {
    Initializer initializer;
    constructNormalDependencyGraph(initializer.getInitializerDependencyGraph());

    ASSERT_OK(initializer.executeInitializers({}));
    initializer.executeInitializers({}).ignore();
}

DEATH_TEST_F(InitializerTest,
             CannotDeinitializeWithoutInitialize,
             "invalid initializer state transition") {
    Initializer initializer;
    constructNormalDependencyGraph(initializer.getInitializerDependencyGraph());

    initializer.executeDeinitializers().ignore();
}

DEATH_TEST_F(InitializerTest, CannotDoubleDeinitialize, "invalid initializer state transition") {
    Initializer initializer;
    constructNormalDependencyGraph(initializer.getInitializerDependencyGraph());
    ASSERT_OK(initializer.executeInitializers({}));
    ASSERT_OK(initializer.executeDeinitializers());
    initializer.executeDeinitializers().ignore();
}

}  // namespace
}  // namespace mongo
