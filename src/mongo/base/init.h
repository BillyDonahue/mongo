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
 * Utility macros for declaring global initializers
 *
 * Should NOT be included by other header files.  Include only in source files.
 *
 * Initializers are arranged in an acyclic directed dependency graph.  Declaring
 * a cycle will lead to a runtime error.
 *
 * Initializer functions take a parameter of type ::mongo::InitializerContext*, and return
 * a Status.  Any status other than Status::OK() is considered a failure that will stop further
 * intializer processing.
 */

#pragma once

#include "mongo/base/deinitializer_context.h"
#include "mongo/base/global_initializer.h"
#include "mongo/base/global_initializer_registerer.h"
#include "mongo/base/initializer.h"
#include "mongo/base/initializer_context.h"
#include "mongo/base/initializer_function.h"
#include "mongo/base/status.h"

#define MONGO_INITIALIZER_UNPACK_(...) __VA_ARGS__
#define MONGO_INITIALIZER_UNPAREN_(x) MONGO_INITIALIZER_UNPACK_(MONGO_INITIALIZER_UNPACK_ x)
#define MONGO_INITIALIZER_DISPATCH_PICKER_(dum1, dum2, dum3, N, ...) N
#define MONGO_INITIALIZER_DISPATCH_(...) \
    MONGO_INITIALIZER_DISPATCH_PICKER_(  \
        __VA_ARGS__, MONGO_INITIALIZER_3, MONGO_INITIALIZER_2, MONGO_INITIALIZER_1)

#define MONGO_INITIALIZER_1(name) MONGO_INITIALIZER_2(name, ("default"))
#define MONGO_INITIALIZER_2(name, prereqs) MONGO_INITIALIZER_3(name, prereqs, ())
#define MONGO_INITIALIZER_3(name, prereqs, dependents)                                    \
    ::mongo::Status MONGO_INITIALIZER_FUNCTION_NAME_(name)(::mongo::InitializerContext*); \
    namespace {                                                                           \
    ::mongo::GlobalInitializerRegisterer _mongoInitializerRegisterer_##name(              \
        std::string(#name),                                                               \
        std::vector<std::string>{MONGO_INITIALIZER_UNPAREN_(prereqs)},                    \
        std::vector<std::string>{MONGO_INITIALIZER_UNPAREN_(dependents)},                 \
        mongo::InitializerFunction(MONGO_INITIALIZER_FUNCTION_NAME_(name)));              \
    }                                                                                     \
    ::mongo::Status MONGO_INITIALIZER_FUNCTION_NAME_(name)

/**
 * MONGO_INITIALIZER(NAME, [PREREQUISITES, [DEPENDENTS]])
 *   Macro to define an initializer that depends on PREREQUISITES and has DEPENDENTS as explicit
 *   dependents.
 *
 * NAME is any name usable as a C++ identifier.
 * PREREQUISITES (optional) is a tuple of 0 or more string literals, i.e., ("a", "b", "c"), or ().
 *    If unspecified, defaults to ("default"). That is, initializer depend on an
 *    initializer called "default" unless they specify explicit prerequisites.
 * DEPENDENTS (optional) is a tuple of 0 or more string literals.
 *    If unspecified, defaults to ().
 *
 * Usage:
 *     MONGO_INITIALIZER(myModule)(::mongo::InitializerContext* context) {
 *         ...
 *     }
 *     MONGO_INITIALIZER(myGlobalStateChecker,
 *                       ("globalStateInitialized", "stacktraces"))(
 *            ::mongo::InitializerContext* context) {
 *     }
 *     MONGO_INITIALIZER(myInitializer,
 *                       ("myPrereq1", "myPrereq2", ...),
 *                       ("myDependent1", "myDependent2", ...))(
 *             ::mongo::InitializerContext* context) {
 *     }
 *
 * At run time, the full set of prerequisites for NAME will be computed as the union of the
 * explicit PREREQUISITES and the set of all other mongo initializers that name NAME in their
 * list of dependents.
 *
 * TODO: May want to be able to name the initializer separately from the function name.
 * A form that takes an existing function or that lets the programmer supply the name
 * of the function to declare would be options.
 */
#define MONGO_INITIALIZER(...) MONGO_INITIALIZER_DISPATCH_(__VA_ARGS__)(__VA_ARGS__)

/**
 * Macro to define an initializer group.
 *
 * An initializer group is an initializer that performs no actions.  It is useful for organizing
 * initialization steps into phases, such as "all global parameter declarations completed", "all
 * global parameters initialized".
 */
#define MONGO_INITIALIZER_GROUP(NAME, PREREQUISITES, DEPENDENTS)                       \
    MONGO_INITIALIZER(NAME, PREREQUISITES, DEPENDENTS)(::mongo::InitializerContext*) { \
        return ::mongo::Status::OK();                                                  \
    }

/**
 * Macro to produce a name for a mongo initializer function for an initializer operation
 * named "NAME".
 */
#define MONGO_INITIALIZER_FUNCTION_NAME_(NAME) _mongoInitializerFunction_##NAME
