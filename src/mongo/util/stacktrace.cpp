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

// stacktrace_${TARGET_OS_FAMILY}.cpp sets default log component to kControl.
// Setting kDefault to preserve previous behavior in (defunct) getStacktraceLogger().
#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/util/stacktrace.h"

#include "mongo/util/log.h"

namespace mongo {

StackTraceSink::~StackTraceSink() = default;

StackTraceSink& StackTraceSink::write(StringData s) {
    doWrite(s);
    return *this;
}

StackTraceSink& StackTraceSink::write(uintptr_t v) {
    char buf[CHAR_BIT * sizeof(v) / 4];  // 4 bits per hexdigit
    char* e = buf + sizeof(buf);
    char* d = e;
    if (v == 0) {
        *--d = '0';
    } else {
        constexpr static char kHex[] = "0123456789ABCDEF";
        while (v) {
            *--d = kHex[v % 16];
            v /= 16;
        }
    }
    return write(StringData(d, e - d));
}

#if !defined(_WIN32)
void FdStackTraceSink::doWrite(StringData s) {
    while (!s.empty()) {
        int n = ::write(_fd, s.rawData(), s.size());
        if (n == -1) {
            if (errno == EINTR) {
                n = 0;
            } else {
                break;
            }
        }
        s = s.substr(n);
    }
}
#endif  // !defined(_WIN32)

void printStackTrace() {
    // NOTE: We disable long-line truncation for the stack trace, because the JSON representation of
    // the stack trace can sometimes exceed the long line limit.
    printStackTrace(log().setIsTruncatable(false).stream());
}

#if defined(_WIN32)

void printWindowsStackTrace(CONTEXT& context) {
    printWindowsStackTrace(context, log().stream());
}

#endif  // defined(_WIN32)

}  // namespace mongo
