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
 * Tools for working with in-process stack traces.
 */

#include <array>
#include <boost/optional.hpp>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "mongo/platform/windows_basic.h"  // for CONTEXT
#endif

#include "mongo/base/string_data.h"
#include "mongo/config.h"
#include "mongo/logger/logstream_builder.h"

// There are a few different stacktrace backends.
#define MONGO_STACKTRACE_BACKEND_NONE 1
#define MONGO_STACKTRACE_BACKEND_LIBUNWIND 2
#define MONGO_STACKTRACE_BACKEND_EXECINFO 3
#define MONGO_STACKTRACE_BACKEND_WINDOWS 4

// The stacktrace backend is selected by OS and by possible presence of libunwind. On Windows,
// BACKEND_WINDOWS is always used. Otherwise, if building with --use-libunwind, we use the
// BACKEND_LIBUNWIND. Otherwise, we have an execinfo.h (libgcc_s) unwinder, we use
// BACKEND_EXECINFO. Finally we fall back to BACKEND_NONE.
#if defined(_WIN32)
#define MONGO_STACKTRACE_BACKEND MONGO_STACKTRACE_BACKEND_WINDOWS
#elif defined(MONGO_USE_LIBUNWIND)
#define MONGO_STACKTRACE_BACKEND MONGO_STACKTRACE_BACKEND_LIBUNWIND
#elif defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
#define MONGO_STACKTRACE_BACKEND MONGO_STACKTRACE_BACKEND_EXECINFO
#else
#define MONGO_STACKTRACE_BACKEND MONGO_STACKTRACE_BACKEND_NONE
#endif

// Headers to support each stacktrace backend.
#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND
#include <libunwind.h>
#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO
#include <execinfo.h>
#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_WINDOWS
#pragma warning(push)
// C4091: 'typedef ': ignored on left of '' when no variable is declared
#pragma warning(disable : 4091)
#include <DbgHelp.h>
#pragma warning(pop)
#endif

namespace mongo {
namespace stack_trace {

// Limit to stacktrace depth
constexpr size_t kFrameMax = 100;

/**
 * Context: A platform-specific stack trace startpoint.
 * For example, on Windows this is holds a CONTEXT.
 * Macro MONGO_STACKTRACE_CONTEXT_INITIALIZE(c) initializes Context `c`,
 *
 * The CONTEXT_INITIALIZE operation can't be a function call, unfortunately, because the
 * Context may become invalid when the function in which it was initialized returns.
 * At least this is the case for the Windows and libunwind backends.
 */
struct Context {

#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO

#define MONGO_STACKTRACE_CONTEXT_INITIALIZE(c)                                 \
    do {                                                                       \
        c.addressesSize = ::backtrace(c.addresses.data(), c.addresses.size()); \
    } while (false)

    std::array<void*, kFrameMax> addresses;
    size_t addressesSize = 0;
    int savedErrno;

#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND

#define MONGO_STACKTRACE_CONTEXT_INITIALIZE(c)      \
    do {                                            \
        c.unwError = unw_getcontext(&c.unwContext); \
    } while (false)

    unw_context_t unwContext;
    int unwError;

#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_WINDOWS

#define MONGO_STACKTRACE_CONTEXT_INITIALIZE(c)                \
    do {                                                      \
        memset(&c.contextRecord, 0, sizeof(c.contextRecord)); \
        c.contextRecord.ContextFlags = CONTEXT_CONTROL;       \
        RtlCaptureContext(&c.contextRecord);                  \
    } while (false)

    CONTEXT contextRecord;

#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_NONE

#define MONGO_STACKTRACE_CONTEXT_INITIALIZE(c) \
    do {                                       \
    } while (false)

#endif  // MONGO_STACKTRACE_BACKEND
};

/** Abstract sink onto which stacktrace is piecewise emitted. */
class Sink {
public:
    Sink& operator<<(StringData v);

private:
    virtual void doWrite(StringData v) = 0;
};

/** A Sink that writes to a std::ostream. */
class OstreamSink : public Sink {
public:
    explicit OstreamSink(std::ostream& os);

private:
    void doWrite(StringData v) override;
    std::ostream& _os;
};

/** LogstreamBuilder has a delicate lifetime. */
class LogstreamBuilderSink : public Sink {
public:
    explicit LogstreamBuilderSink(logger::LogstreamBuilder&& lsb) : _lsb{std::move(lsb)} {}

    void doWrite(StringData v) override {
        _lsb.stream() << v;
    }

private:
    logger::LogstreamBuilder _lsb;
};

/** Abstract allocator to provide moderate memory in a potentially AS-Safe context. */
class Allocator {
public:
    virtual ~Allocator() = default;
    // No alignment guarantees
    virtual void* allocate(size_t n) = 0;
    virtual void deallocate(void*) = 0;
};

class SequentialAllocator : public Allocator {
public:
    SequentialAllocator(char* buf, size_t bufSize) : _buf(buf), _bufSize(bufSize), _next(_buf) {}
    SequentialAllocator(const SequentialAllocator&) = delete;
    SequentialAllocator& operator=(const SequentialAllocator&) = delete;
    void* allocate(size_t n) override {
        if (_next + n > _buf + _bufSize) {
            return nullptr;
        }
        void* r = _next;
        _next += n;
        return r;
    }
    void deallocate(void*) override {}

    void reset() {
        _next = _buf;
    }

    size_t capacity() const {
        return _bufSize;
    }

    size_t used() const {
        return static_cast<size_t>(_next - _buf);
    }

private:
    char* _buf;
    size_t _bufSize;
    char* _next;
};

/**
 * Metadata about an instruction address. It may have an enclosing shared object file.
 * It may have an enclosing symbol (function name). The soFile and symbol exist
 * independently. Presence of one does not imply the presence of the other.
 */
struct AddressMetadata {
    struct NameBase {
        StringData name;
        uintptr_t base = 0;
        char* nameAllocation = nullptr;
    };

    AddressMetadata allocateCopy(Allocator& alloc) const;
    void deallocate(Allocator& alloc);

    uintptr_t address{};
    boost::optional<NameBase> soFile{};
    boost::optional<NameBase> symbol{};
};

constexpr StringData kUnknownFileName = "???"_sd;

void printOneMetadata(const AddressMetadata& f, Sink& sink);
void mergeDlInfo(AddressMetadata& f);

class Tracer {
public:
    struct Options {
        // Options for print
        bool withProcessInfo = true;
        bool withHumanReadable = true;
        bool trimSoMap = true;  // only include the somap entries relevant to the backtrace

        // Options backtrace
        bool dlAddrOnly = false;  // dladdr is fast but inaccurate compared to unw_get_proc_name

        Sink* errSink = nullptr;
        Allocator* alloc = nullptr;
    };

    Tracer() = default;
    explicit Tracer(Options options) : options{std::move(options)} {}

    /** Write a trace of the stack captured in `context` to the `sink`. */
    void print(Context& context, Sink& sink) const;

    /** Like print, but fills addrs[capacity] with addresses. */
    size_t backtrace(void** addrs, size_t capacity) const;

    /** Richer than plain `backtrace()`. Fills `addrs[capacity]` and `meta[capacity]` */
    size_t backtraceWithMetadata(void** addrs, AddressMetadata* meta, size_t capacity) const;

    /** Cleanup and deallocate a metadata array previously filled by backtraceWithMetadata. */
    void destroyMetadata(AddressMetadata* meta, size_t size) const;

    /** Fill meta with metadata about `addr`. Return 0 on success.
     *  Clean up with meta->deallocate(alloc), where alloc is Tracer's allocator.
     */
    int getAddrInfo(void* addr, AddressMetadata* meta) const;

    Options options;
};

#ifdef __linux__
struct ThreadInfo {
    int tid;
};

/**
 * Execute a callback `f(arg)` on every thread in the process, in a signal handler.
 * Might miss a few threads. Linux only.  Defined in stacktrace_foreach_thread.cpp.
 */
void forEachThread(void (*f)(const ThreadInfo*, void*), void* arg);
#endif  // __linux__

LogstreamBuilderSink makeDefaultSink();

}  // namespace stack_trace

/** Some `stack_trace::Tracer{...}::print` callers with different defaults. */

/** Make a Context and print its stack trace to `os`. */
void printStackTrace(std::ostream& os);

/** Goes to `mongo::log()` stream for the `kDefault` component. */
void printStackTrace();

}  // namespace mongo
