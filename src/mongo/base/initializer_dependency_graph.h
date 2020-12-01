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
#include <utility>
#include <vector>

#include "mongo/base/dependency_graph.h"
#include "mongo/base/initializer_function.h"
#include "mongo/base/status.h"

namespace mongo {

/**
 * Representation of a dependency graph of "initialization operations."
 *
 * Each operation has a unique name, a function object implementing the operation's behavior,
 * and a set of prerequisite operations, which may be empty.  A legal graph contains no cycles.
 *
 * Instances of this class are used in two phases.  In the first phase, the graph is "unfrozen",
 * which permits it to be constructed by repeated calls to addInitializer().  In the second phase,
 * the graph is "frozen" by calling frozen(), which prevents the addition of any further
 * initializers to the graph.  A user can then call the topSort() method to produce an
 * initialization order that respects the dependencies among operations, and then use the
 * getInitializerFunction() to get the behavior function for each operation, in turn.
 *
 * Concurrency Notes: The user is responsible for synchronization.  Multiple threads may
 * simultaneously call the const functions, getInitializerFunction and topSort, on the same instance
 * of InitializerDependencyGraph.  However, no thread may call addInitializer or freeze while any
 * thread is executing those functions, addInitializer or freeze on the same instance.
 */
class InitializerDependencyGraph {
public:
    class Node : public DependencyGraph::Payload {
    public:
        bool isInitialized() const {
            return initialized;
        }
        void setInitialized(bool value) {
            initialized = value;
        };

        const InitializerFunction& getInitializerFunction() const {
            return initFn;
        }
        const DeinitializerFunction& getDeinitializerFunction() const {
            return deinitFn;
        }

    private:
        friend class InitializerDependencyGraph;
        InitializerFunction initFn;
        DeinitializerFunction deinitFn;
        bool initialized{false};
    };

    /**
     * Add a new initializer node, named "name", to the dependency graph, with the given
     * behavior, `initFn`, `deiniFn`, and with the given `prerequisites` and `dependents`,
     * which are the names of other initializers which will be in the graph when `topSort`
     * is called.
     *
     * - Throws with `ErrorCodes::CannotMutateObject` if the graph is frozen.
     *
     * - Throws `ErrorCodes::BadValue` if `initFn` converts to false (e.g. null valued).
     *
     * Note that cycles in the dependency graph are not discovered by this
     * function. Rather, they're discovered by topSort, below.
     */
    void addInitializer(std::string name,
                        InitializerFunction initFn,
                        DeinitializerFunction deinitFn,
                        std::vector<std::string> prerequisites,
                        std::vector<std::string> dependents);

    /**
     * Given a dependency operation node named "name", return its behavior function.  Returns
     * a value that evaluates to "false" in boolean context, otherwise.
     */
    Node* getInitializerNode(const std::string& name);

    /**
     * Returns a topological sort of the dependency graph, represented
     * as an ordered vector of node names.
     *
     * Throws with `ErrorCodes::GraphContainsCycle` if the graph contains a cycle.
     * If a `cycle` is given, it will be overwritten with the node sequence involved.
     *
     * Throws with `ErrorCodes::BadValue` if any node in the graph names a
     * prerequisite that is missing from the graph.
     */
    std::vector<std::string> topSort() const;

    /**
     * Called to mark the end of the period when nodes are allowed to be added to the graph.
     * The graph is effectively read-only after this point.
     */
    void freeze() {
        _frozen = true;
    }

    /**
     * Returns true if this graph has been frozen.
     */
    bool frozen() const {
        return _frozen;
    }

private:
    /**
     * Map of all named nodes.  Nodes named as prerequisites or dependents but not explicitly
     * added via addInitializer will either be absent from this map or be present with
     * NodeData::fn set to a false-ish value.
     */
    DependencyGraph _graph;

    /**
     * If true, then the graph is "frozen" (ie. effectively read-only), and adding initializer nodes
     * is not allowed.
     */
    bool _frozen{false};
};

}  // namespace mongo
