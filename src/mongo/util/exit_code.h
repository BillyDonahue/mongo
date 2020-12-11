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

/**
 * Mongo exit codes.
 */

namespace mongo {

#include "mongo/base/string_data.h"

#define EXPAND_EXIT_CODE_(X)                                                           \
    X(EXIT_CLEAN, 0)                                                                   \
    X(EXIT_BADOPTIONS, 2)                                                              \
    X(EXIT_REPLICATION_ERROR, 3)                                                       \
    X(EXIT_NEED_UPGRADE, 4)                                                            \
    X(EXIT_SHARDING_ERROR, 5)                                                          \
    X(EXIT_KILL, 12)                                                                   \
    X(EXIT_ABRUPT, 14)                                                                 \
    X(EXIT_NTSERVICE_ERROR, 20)                                                        \
    X(EXIT_JAVA, 21)                                                                   \
    X(EXIT_OOM_MALLOC, 42)                                                             \
    X(EXIT_OOM_REALLOC, 43)                                                            \
    X(EXIT_FS, 45)                                                                     \
    X(EXIT_CLOCK_SKEW, 47) /* OpTime clock skew (deprecated) */                        \
    X(EXIT_NET_ERROR, 48)                                                              \
    X(EXIT_WINDOWS_SERVICE_STOP, 49)                                                   \
    X(EXIT_POSSIBLE_CORRUPTION, 60) /* Detected corruption, e.g. buf overflow */       \
    X(EXIT_WATCHDOG, 61)            /* Internal Watchdog has terminated mongod */      \
    X(EXIT_NEED_DOWNGRADE, 62)      /* Data files incompatible with this executable */ \
    X(EXIT_THREAD_SANITIZER, 66)    /* Thread Sanitizer failures */                    \
    X(EXIT_UNCAUGHT, 100)           /* Top level exception that wasn't caught */       \
    X(EXIT_TEST, 101)                                                                  \
    /**/

// clang-format off

enum ExitCode : int {
#define X_(code, value) \
    code = value,
EXPAND_EXIT_CODE_(X_)
#undef X_
};

inline bool isValid(ExitCode code) {
    switch (code) {
#define X_(c, v) \
    case c: \
        return true;
EXPAND_EXIT_CODE_(X_)
#undef X_
        default:
            return false;
    }
}

inline StringData toStringData(ExitCode code) {
    switch (code) {
#define X_(c, v) \
    case c: \
        return #c ""_sd;
EXPAND_EXIT_CODE_(X_)
#undef X_
    }
    return ""_sd;
}

// clang-format on

#undef EXPAND_EXIT_CODE_

}  // namespace mongo
