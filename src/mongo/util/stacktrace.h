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
 * Tools for working with in-process stack traces.
 */

#pragma once

#include <iosfwd>
#include <type_traits>

#include "mongo/base/string_data.h"

#if defined(_WIN32)
#include "mongo/platform/windows_basic.h"  // for CONTEXT
#endif

namespace mongo {

// A simple zero-allocation streamlike object that just does hex uints or pointers, and StringData.
class StackTraceSink {
public:
    StackTraceSink& write(StringData v);
    StackTraceSink& write(const char* v) { return write(StringData(v)); }
    StackTraceSink& write(uintptr_t v);

    template <typename T>
    StackTraceSink& write(T* v) {
        if constexpr (std::is_same_v<std::decay_t<T>, char>) {
            return write(StringData(v));
        } else {
            return write(reinterpret_cast<uintptr_t>(v));
        }
    }

    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    StackTraceSink& write(const T& v) {
        if constexpr (std::is_same_v<T, char>) {
            return write(StringData(&v, 1));
        } else {
            return write(static_cast<uintptr_t>(v));
        }
    }

    friend StackTraceSink& operator<<(StackTraceSink& sink, StringData v) { return sink.write(v); }
    friend StackTraceSink& operator<<(StackTraceSink& sink, char v) { return sink.write(v); }
    friend StackTraceSink& operator<<(StackTraceSink& sink, const char* v) { return sink.write(v); }
    friend StackTraceSink& operator<<(StackTraceSink& sink, uintptr_t v) { return sink.write(v); }
    template <typename T>
    friend StackTraceSink& operator<<(StackTraceSink& sink, T* v) { return sink.write(v); }
    template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    friend StackTraceSink& operator<<(StackTraceSink& sink, const T& v) {
        return sink.write(v);
    }

protected:
    virtual ~StackTraceSink();
    virtual void doWrite(StringData s) = 0;
};

// Print stack trace information to "os", default to the log stream.
void printStackTrace(std::ostream& os);
void printStackTrace();

// Signal-safe variant.
void printStackTraceFromSignal(std::ostream& os);

#if defined(_WIN32)
// Print stack trace (using a specified stack context) to "os", default to the log stream.
void printWindowsStackTrace(CONTEXT& context, std::ostream& os);
void printWindowsStackTrace(CONTEXT& context);
#endif

}  // namespace mongo
