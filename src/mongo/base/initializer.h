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

#include <string>
#include <vector>

#include "mongo/base/initializer_context.h"
#include "mongo/base/initializer_dependency_graph.h"
#include "mongo/base/status.h"

namespace mongo {

/**
 * Class representing an initialization process.
 *
 * Such a process is described by a directed acyclic graph of initialization operations, the
 * InitializerDependencyGraph.  One constructs an initialization process by adding nodes and
 * edges to the graph.  Then, one executes the process, causing each initialization operation to
 * execute in an order that respects the programmer-established prerequistes.
 */
class Initializer {
public:
    /**
     * Add a new initializer node, named `name`, to the dependency graph.
     * It represents a subsystem that is brought up with `initFn` and
     * brought down with `deinitFn`, which may be null-valued.
     *
     * See `InitializerDependencyGraph::addInitializer` for more details.
     */
    void addInitializer(std::string name,
                        InitializerFunction initFn,
                        DeinitializerFunction deinitFn,
                        std::vector<std::string> prerequisites,
                        std::vector<std::string> dependents);

    /**
     * Execute the initializer process, using the given args as input.
     *
     * Throws on initialization failures, or on invalid call sequences
     * (double-init, double-deinit, etc) and the thing being initialized should
     * be considered dead in the water.
     */
    void executeInitializers(const std::vector<std::string>& args);

    void executeDeinitializers();

    /** 
     * Returns the function mapped to `name`, for testing only.
     *
     * Throws with `ErrorCodes::BadValue` if name is not mapped to a node.
     */
    InitializerFunction getInitializerFunctionForTesting(const std::string& name);

private:
    enum class State {
        kUninitialized,
        kInitializing,
        kInitialized,
        kDeinitializing,
    };
    void _transition(State expected, State next);

    InitializerDependencyGraph _graph;
    std::vector<std::string> _sortedNodes;
    State _lifecycleState{State::kUninitialized};
};

/**
 * Run the global initializers.
 *
 * It's a programming error for this to fail, but if it does it will return a status other
 * than Status::OK.
 *
 * This means that the few initializers that might want to terminate the program by failing
 * should probably arrange to terminate the process themselves.
 */
Status runGlobalInitializers(const std::vector<std::string>& argv);

/**
 * Same as runGlobalInitializers(), except prints a brief message to std::cerr
 * and terminates the process on failure.
 */
void runGlobalInitializersOrDie(const std::vector<std::string>& argv);

/**
 * Run the global deinitializers. They will execute in reverse order from initialization.
 *
 * It's a programming error for this to fail, but if it does it will return a status other
 * than Status::OK.
 *
 * This means that the few initializers that might want to terminate the program by failing
 * should probably arrange to terminate the process themselves.
 */
Status runGlobalDeinitializers();

}  // namespace mongo
