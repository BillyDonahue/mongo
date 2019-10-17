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

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>
#include <boost/optional.hpp>

#if defined(_WIN32)
#include "mongo/platform/windows_basic.h"  // for CONTEXT
#endif

#include "mongo/base/string_data.h"
#include "mongo/config.h"

#define MONGO_STACKTRACE_BACKEND_NONE 1
#define MONGO_STACKTRACE_BACKEND_LIBUNWIND 2
#define MONGO_STACKTRACE_BACKEND_EXECINFO 3
#define MONGO_STACKTRACE_BACKEND_WINDOWS 4

#if defined(_WIN32)
#define MONGO_STACKTRACE_BACKEND MONGO_STACKTRACE_BACKEND_WINDOWS
#elif defined(MONGO_USE_LIBUNWIND)
#define MONGO_STACKTRACE_BACKEND MONGO_STACKTRACE_BACKEND_LIBUNWIND
#elif defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
#define MONGO_STACKTRACE_BACKEND MONGO_STACKTRACE_BACKEND_EXECINFO
#else
#define MONGO_STACKTRACE_BACKEND MONGO_STACKTRACE_BACKEND_NONE
#endif

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

/**
 * Tools for working with in-process stack traces.
 */

namespace mongo {
namespace stack_trace {

// Limit to stacktrace depth
constexpr size_t kFrameMax = 100;

// World's dumbest "vector". Doesn't allocate.
template <typename T, size_t N>
struct ArrayAndSize {
    using iterator = typename std::array<T, N>::iterator;
    using reference = typename std::array<T, N>::reference;
    using const_reference = typename std::array<T, N>::const_reference;

    void resize(size_t n) {
        _n = n;
    }
    size_t size() const {
        return _n;
    }
    bool empty() const {
        return size() == 0;
    }
    size_t capacity() const {
        return _arr.size();
    }
    auto data() {
        return _arr.data();
    }
    auto data() const {
        return _arr.data();
    }

    auto begin() {
        return _arr.begin();
    }
    auto end() {
        return _arr.begin() + _n;
    }
    reference operator[](size_t i) {
        return _arr[i];
    }
    auto begin() const {
        return _arr.begin();
    }
    auto end() const {
        return _arr.begin() + _n;
    }
    const_reference operator[](size_t i) const {
        return _arr[i];
    }
    void push_back(const T& v) {
        _arr[_n++] = v;
    }

    std::array<T, N> _arr;
    size_t _n = 0;
};

/**
 * Context: A platform-specific stack trace startpoint.
 * For example, on Windows this is holds a CONTEXT.
 * Macro MONGO_STACKTRACE_CONTEXT_INITIALIZE(c) initializes Context `c`,
 */
struct Context {

#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO

#define MONGO_STACKTRACE_CONTEXT_INITIALIZE(c)                                       \
    do {                                                                             \
        c.addresses.resize(::backtrace(c.addresses.data(), c.addresses.capacity())); \
    } while (false)

    ArrayAndSize<void*, kFrameMax> addresses;
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

struct BacktraceSymbolsResult {
    struct _Impl {
        _Impl();
        _Impl(_Impl&&);
        ~_Impl();
#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND
        std::vector<std::string> namesVec;
        std::unique_ptr<const char*[]> namesPtrs;
#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO
        struct FreeDeleter {
            void operator()(const char** p) const {
                free(p);
            }
        };
        std::unique_ptr<const char*, FreeDeleter> namesPtrs;
        size_t namesPtrSize;
#endif
    };
    const char** names() const;
    size_t size() const;
    _Impl _impl;
};

class Tracer {
public:
    struct NameBase {
        StringData name;
        uintptr_t base;
    };

    // Metadata about an instruction address.
    // Beyond that, it may have an enclosing shared object file.
    // Further, it may have an enclosing symbol within that shared object.
    struct AddressMetadata {
        uintptr_t address{};
        boost::optional<NameBase> soFile{};
        boost::optional<NameBase> symbol{};
    };

    struct Options {
        // Options for print
        bool withProcessInfo = true;
        bool withHumanReadable = true;
        bool trimSoMap = true;  // only include the somap entries relevant to the backtrace
        Sink* sink = nullptr;

        // Options backtrace
        bool dlAddrOnly = false;  // dladdr is fast but inaccurate compared to unw_get_proc_name
    };

    Tracer() = default;
    explicit Tracer(Options options) : options{std::move(options)} {}

    /** Write a trace of the stack captured in `context` to the `sink`. */
    void print(Context& context, Sink& sink) const;

    /** Like print, but fills Options.backtraceBuf with addresses. */
    size_t backtrace(void** buf, size_t bufSize);


    /** Fill meta with metadata about `addr`. Return 0 on success. */
    int getAddrInfo(void* addr, AddressMetadata* meta) const;

    /**
     * For the `buf[bufSize]` addresses obtained from `stack_trace::backtrace`, obtain an
     * array of strings representing those addresses, managed by the returned object.
     */
    BacktraceSymbolsResult backtraceSymbols(void* const* buf, size_t bufSize);

    Options options;
};

}  // namespace stack_trace

/** Some `stack_trace::Tracer::print` callers with different defaults. */
void printStackTrace(std::ostream& os);

/** Goes to `mongo::log()` stream for the `kDefault` component. */
void printStackTrace();

}  // namespace mongo
