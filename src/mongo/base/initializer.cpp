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
#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/base/initializer.h"

#include <fmt/format.h>
#include <iostream>

#include "mongo/base/deinitializer_context.h"
#include "mongo/base/global_initializer.h"
#include "mongo/base/initializer_context.h"
#include "mongo/logv2/log.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/str.h"

namespace mongo {

void Initializer::_transition(State expected, State next) {
    if (_lifecycleState != expected)
        uasserted(ErrorCodes::IllegalOperation,
                  format(FMT_STRING("invalid initializer state transition {}->{}"),
                         _lifecycleState,
                         next));
    _lifecycleState = next;
}

void Initializer::addInitializer(std::string name,
                                 InitializerFunction initFn,
                                 DeinitializerFunction deinitFn,
                                 std::vector<std::string> prerequisites,
                                 std::vector<std::string> dependents) {
    uassert(ErrorCodes::CannotMutateObject, "Initializer dependency graph is frozen",
            _lifecycleState == State::kNeverInitialized);
    _graph.addInitializer(std::move(name),
                          std::move(initFn),
                          std::move(deinitFn),
                          std::move(prerequisites),
                          std::move(dependents));
}


void Initializer::executeInitializers(const std::vector<std::string>& args) {
    if (_lifecycleState == State::kNeverInitialized)
        _transition(State::kNeverInitialized, State::kUninitialized);  // freeze
    _transition(State::kUninitialized, State::kInitializing);

    if (_sortedNodes.empty())
        _sortedNodes = _graph.topSort();

    InitializerContext context(args);

    for (const auto& nodeName : _sortedNodes) {
        auto* node = _graph.getInitializerNode(nodeName);

        if (node->initialized)
            continue;  // Legacy initializer without re-initialization support.

        uassert(ErrorCodes::InternalError,
                format(FMT_STRING("node has no init function: \"{}\""), nodeName),
                node->initFn);
        node->initFn(&context);

        node->initialized = true;
    }

    _transition(State::kInitializing, State::kInitialized);

    // The order of the initializers is non-deterministic, so make it available.
    // Must be after verbose has been parsed, or the Debug(2) severity won't be visible.
    LOGV2_DEBUG_OPTIONS(4777800,
                        2,
                        {logv2::LogTruncation::Disabled},
                        "Ran initializers",
                        "nodes"_attr = _sortedNodes);
}

void Initializer::executeDeinitializers() {
    _transition(State::kInitialized, State::kDeinitializing);

    DeinitializerContext context{};

    // Execute deinitialization in reverse order from initialization.
    for (auto it = _sortedNodes.rbegin(), end = _sortedNodes.rend(); it != end; ++it) {
        auto* node = _graph.getInitializerNode(*it);
        if (node->deinitFn) {
            node->deinitFn(&context);
            node->initialized = false;
        }
    }

    _transition(State::kDeinitializing, State::kUninitialized);
}

Status runGlobalInitializers(const std::vector<std::string>& argv) {
    try {
        getGlobalInitializer().executeInitializers(argv);
        return Status::OK();
    } catch (const DBException& ex) {
        return ex.toStatus();
    }
}

Status runGlobalDeinitializers() {
    try {
        getGlobalInitializer().executeDeinitializers();
        return Status::OK();
    } catch (const DBException& ex) {
        return ex.toStatus();
    }
}

void runGlobalInitializersOrDie(const std::vector<std::string>& argv) {
    if (Status status = runGlobalInitializers(argv); !status.isOK()) {
        std::cerr << "Failed global initialization: " << status << std::endl;
        quickExit(1);
    }
}

}  // namespace mongo
