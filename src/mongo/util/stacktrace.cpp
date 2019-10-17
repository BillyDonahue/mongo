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

namespace mongo {
namespace stack_trace {

Sink& Sink::operator<<(StringData v) {
    doWrite(v);
    return *this;
}

OstreamSink::OstreamSink(std::ostream& os) : _os(os) {}
void OstreamSink::doWrite(StringData v) {
    _os << v;
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

}  // namespace stack_trace

void printStackTrace(std::ostream& os) {
    stack_trace::Context context;
    MONGO_STACKTRACE_CONTEXT_INITIALIZE(context);
    stack_trace::OstreamSink sink(os);
    stack_trace::Tracer{}.print(context, sink);
}

void printStackTrace() {
    printStackTrace(log().setIsTruncatable(false).stream());
}

}  // namespace mongo
