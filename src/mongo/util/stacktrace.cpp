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
#include "mongo/util/stacktrace_json.h"

#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND || \
    MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO
#include <dlfcn.h>
#endif

namespace mongo {
namespace stack_trace {

AddressMetadata AddressMetadata::allocateCopy(Allocator& alloc) const {
    auto allocateField = [&](auto f) {
        if (f) {
            auto& src = f->name;
            if (src.size()) {
                auto p = static_cast<char*>(alloc.allocate(src.size()));
                if (p) {
                    std::copy(src.begin(), src.end(), p);
                    f->name = StringData(p, src.size());
                } else {
                    f->name = StringData{};
                }
                f->nameAllocation = p;
            }
        }
        return f;
    };
    AddressMetadata r;
    r.address = address;
    r.soFile = allocateField(soFile);
    r.symbol = allocateField(symbol);
    return r;
}

void AddressMetadata::deallocate(Allocator& alloc) {
    auto deallocateField = [&](auto& f) {
        if (!f)
            return;
        if (f->nameAllocation) {
            alloc.deallocate(f->nameAllocation);
        }
    };
    deallocateField(soFile);
    deallocateField(symbol);
}

Sink& Sink::operator<<(StringData v) {
    doWrite(v);
    return *this;
}

OstreamSink::OstreamSink(std::ostream& os) : _os(os) {}
void OstreamSink::doWrite(StringData v) {
    _os << v;
}

namespace {
// E.g., for "/foo/bar/my.txt", returns "my.txt".
StringData getBaseName(StringData path) {
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos)
        return path;
    return path.substr(lastSlash + 1);
}
}  // namespace

void printOneMetadata(const AddressMetadata& f, Sink& sink) {
    sink << " ";
    if (f.soFile) {
        sink << getBaseName(f.soFile->name);
        sink << "(";
        if (f.symbol) {
            sink << f.symbol->name << "+0x" << Hex(f.address - f.symbol->base).str();
        } else {
            // No symbol, so fall back to the `soFile` offset.
            sink << "+0x" << Hex(f.address - f.soFile->base).str();
        }
        sink << ")";
    } else {
        // Not even shared object information, just punt with unknown filename (SERVER-43551)
        sink << kUnknownFileName;
    }
    sink << " [0x" << Hex(f.address).str() << "]\n";
}

void mergeDlInfo(AddressMetadata& f) {
#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND || \
    MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO
    Dl_info dli;
    // `man dladdr`:
    //   On success, these functions return a nonzero value.  If the address
    //   specified in addr could be matched to a shared object, but not to a
    //   symbol in the shared object, then the info->dli_sname and
    //   info->dli_saddr fields are set to NULL.
    if (dladdr(reinterpret_cast<void*>(f.address), &dli) == 0) {
        return;  // f.address doesn't map to a shared object
    }
    if (!f.soFile) {
        f.soFile =
            AddressMetadata::NameBase{dli.dli_fname, reinterpret_cast<uintptr_t>(dli.dli_fbase)};
    }
    if (!f.symbol) {
        if (dli.dli_saddr) {
            // matched to a symbol in the shared object
            f.symbol = AddressMetadata::NameBase{dli.dli_sname,
                                                 reinterpret_cast<uintptr_t>(dli.dli_saddr)};
        }
    }
#endif  // MONGO_STACKTRACE_BACKEND
}

#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_NONE

void Tracer::print(Context& context, Sink& sink) const {
    sink << "This platform does not support printing stacktraces\n";
}

size_t Tracer::backtrace(void** addrs, size_t capacity) const {
    return 0;
}

size_t Tracer::backtraceWithMetadata(void** addrs, AddressMetadata* meta, size_t capacity) const {
    return 0;
}

int Tracer::getAddrInfo(void* addr, AddressMetadata* meta) const {
    *meta = {};
    return -1;
}

#endif  // MONGO_STACKTRACE_BACKEND

/** Cleanup and deallocate a metadata array previously filled by backtraceWithMetadata. */
void Tracer::destroyMetadata(AddressMetadata* meta, size_t size) const {
    if (options.alloc) {
        for (size_t i = 0; i < size; ++i) {
            meta[i].deallocate(*options.alloc);
        }
    }
}

LogstreamBuilderSink makeDefaultSink() {
    auto lsb = log();
    lsb.setIsTruncatable(false);
    return stack_trace::LogstreamBuilderSink{std::move(lsb)};
}

}  // namespace stack_trace

void printStackTrace(std::ostream& os) {
    stack_trace::Context context;
    MONGO_STACKTRACE_CONTEXT_INITIALIZE(context);
    auto sink = stack_trace::OstreamSink(os);
    stack_trace::Tracer{}.print(context, sink);
}

void printStackTrace() {
    stack_trace::Context context;
    MONGO_STACKTRACE_CONTEXT_INITIALIZE(context);
    auto sink = stack_trace::makeDefaultSink();
    stack_trace::Tracer{}.print(context, sink);
}

}  // namespace mongo
