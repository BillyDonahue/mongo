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

OstreamSink::OstreamSink(std::ostream& os) : _os(os) {}
void OstreamSink::doWrite(StringData v) {
    _os << v;
}

void print(Options& options) {
    // If the caller hasn't provided a options.context, make one and reenter.
    if (!options.context) {
        // Set a context and reenter this function, because we can't access the context
        // after exiting the function that captured it.
        Context context;
        MONGO_STACKTRACE_CONTEXT_INITIALIZE(context);
        options.context = &context;
        print(options);
        return;
    }
    if (!options.sink) {
        // Set a sink and reenter this function.
        // Immediately-invoked lambda preserves the log() temporary.
        // We disable long-line truncation for the stack trace, because the JSON
        // representation of the stack trace can sometimes exceed the long line limit.
        [&](std::ostream& stream) {
            OstreamSink sink(stream);
            options.sink = &sink;
            print(options);
        }(log().setIsTruncatable(false).stream());
        return;
    }
    detail::print(options);
}

size_t backtrace(BacktraceOptions& options, void** buf, size_t bufSize) {
    return detail::backtrace(options, buf, bufSize);
}

#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND
const char** BacktraceSymbolsResult::names() const {
    return _impl.namesPtrs.get();
}
size_t BacktraceSymbolsResult::size() const {
    return _impl.namesVec.size();
}
BacktraceSymbolsResult::_Impl::_Impl(_Impl&&) = default;
#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO
const char** BacktraceSymbolsResult::names() const {
    return _impl.namesPtrs.get();
}
size_t BacktraceSymbolsResult::size() const {
    return _impl.namesPtrs.get() ? _impl.namesPtrSize : 0;
}
BacktraceSymbolsResult::_Impl::_Impl(_Impl&&) = default;
#else
const char** BacktraceSymbolsResult::names() const {
    return nullptr;
}
size_t BacktraceSymbolsResult::size() const {
    return 0;
}
BacktraceSymbolsResult::_Impl::_Impl(_Impl&&) = default;
#endif

BacktraceSymbolsResult::_Impl::_Impl() = default;
BacktraceSymbolsResult::_Impl::~_Impl() = default;

BacktraceSymbolsResult backtraceSymbols(BacktraceOptions& options,
                                        void* const* buf,
                                        size_t bufSize) {
    return detail::backtraceSymbols(options, buf, bufSize);
}

}  // namespace mongo::stack_trace
