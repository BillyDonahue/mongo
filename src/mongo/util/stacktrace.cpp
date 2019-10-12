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

#include "mongo/util/stacktrace.h"
#include "mongo/platform/basic.h"
#include "mongo/util/log.h"

namespace mongo::stack_trace {

Sink& Sink::operator<<(StringData v) {
    doWrite(v);
    return *this;
}
Sink& Sink::operator<<(uint64_t v) {
    doWrite(v);
    return *this;
}

OstreamSink::OstreamSink(std::ostream& os) : _os(os) {}
void OstreamSink::doWrite(StringData v) {
    _os << v;
}
void OstreamSink::doWrite(uint64_t v) {
    _os << v;
}

namespace {

enum class Action { kPrint, kBacktrace };

template <Action action>
void doAction(Options& options) {
    if (!options.context) {
        // Set a context and reenter this function, because we can't access the context
        // after exiting the function that captured it.
        Context context;
#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND
        context.unwError = unw_getcontext(&context.unwContext);
#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO
        context.addresses.resize(
            ::backtrace(context.addresses.data(), context.addresses.capacity()));
#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_WINDOWS
        memset(&context.contextRecord, 0, sizeof(context.contextRecord));
        context.contextRecord.ContextFlags = CONTEXT_CONTROL;
        RtlCaptureContext(&context.contextRecord);
#endif
        options.context = &context;
        doAction<action>(options);
        return;
    }
    if (action == Action::kPrint) {
        if (!options.sink) {
            // Set a sink and reenter this function.
            // Immediately-invoked lambda preserves the log() temporary.
            // We disable long-line truncation for the stack trace, because the JSON
            // representation of the stack trace can sometimes exceed the long line limit.
            [&](std::ostream& stream) {
                OstreamSink sink(stream);
                options.sink = &sink;
                doAction<action>(options);
            }(log().setIsTruncatable(false).stream());
            return;
        }
        detail::printInternal(options);
    } else if (action == Action::kBacktrace) {
        detail::backtraceInternal(options);
    }
}

}  // namespace

void print(Options& options) {
    doAction<Action::kPrint>(options);
}

size_t backtrace(Options& options, void** buf, size_t bufSize) {
    size_t r = 0;
    options.backtraceBuf = buf;
    options.backtraceBufSize = bufSize;
    options.backtraceOut = &r;
    doAction<Action::kBacktrace>(options);
    return r;
}

}  // namespace mongo::stack_trace
