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
#include "mongo/base/initializer_function.h"
#include "mongo/base/status.h"

namespace mongo {

/**
 * Class representing an initialization process.
 *
 * Such a process is described by a directed acyclic graph of initialization operations, the
 * InitializerDependencyGraph. One constructs an initialization process by adding nodes and
 * edges to the graph.  Then, one executes the process, causing each initialization operation to
 * execute in an order that respects the programmer-established prerequistes.
 *
 * The initialize and delinitialize process can repeat, a features which
 * supports embedded contexts.  However, the graph cannot be modified with
 * `addInitializer` after the first initialization. Latecomers are rejected.
 */
class Initializer {
public:
    Initializer();
    ~Initializer();

    /**
     * Add a new initializer node, named `name`, to the dependency graph.
     * It represents a subsystem that is brought up with `initFn` and
     * brought down with `deinitFn`, which may be null-valued.
     *
     * Can be called up until the first call to `executeInitializers`.
     *
     * - Throws with `ErrorCodes::CannotMutateObject` if the graph is frozen.
     */
    void addInitializer(std::string name,
                        InitializerFunction initFn,
                        DeinitializerFunction deinitFn,
                        std::vector<std::string> prerequisites,
                        std::vector<std::string> dependents);

    /**
     * Execute the initializer process, using the given args as input.
     * This call freezes the graph, so that addInitializer will reject any latecomers.
     *
     * Throws on initialization failures, or on invalid call sequences
     * (double-init, double-deinit, etc) and the thing being initialized should
     * be considered dead in the water.
     */
    void executeInitializers(const std::vector<std::string>& args);

    /**
     * Executes all deinit functions in reverse order from init order.
     * Note that this does not unfreeze the graph. Freezing is permanent.
     */
    void executeDeinitializers();

    /**
     * Returns the function mapped to `name`, for testing only.
     *
     * Throws with `ErrorCodes::BadValue` if name is not mapped to a node.
     */
    InitializerFunction getInitializerFunctionForTesting(const std::string& name);

private:
    class Graph;

    enum class State {
        kNeverInitialized,  ///< still accepting addInitializer calls
        kUninitialized,
        kInitializing,
        kInitialized,
        kDeinitializing,
    };

    void _transition(State expected, State next);

    std::unique_ptr<Graph> _graph;  // pimpl
    std::vector<std::string> _sortedNodes;
    State _lifecycleState = State::kNeverInitialized;
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
