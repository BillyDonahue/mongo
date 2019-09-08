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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kControl

#include "mongo/platform/basic.h"

#include "mongo/util/stacktrace.h"
#include "mongo/util/stacktrace_somap.h"

#include <array>
#include <climits>
#include <cstdlib>
#include <dlfcn.h>
#include <iomanip>
#include <iostream>
#include <string>

#include "mongo/base/init.h"
#include "mongo/config.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/version.h"

#if defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE) && 0
#undef MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE
#endif

#if defined(MONGO_USE_LIBUNWIND) && 0
#undef MONGO_USE_LIBUNWIND
#endif

#if defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
#include <execinfo.h>
#endif
#if defined(MONGO_USE_LIBUNWIND)
#include <libunwind.h>
#endif

namespace mongo {
namespace {
namespace stacktrace_detail {

constexpr bool withProcessInfo = false;

constexpr int kFrameMax = 100;
constexpr size_t kSymbolMax = 512;
constexpr size_t kPathMax = PATH_MAX;
constexpr char kUnknownFileName[] = "???";
constexpr ptrdiff_t kOffsetUnknown = -1;

// E.g., for "/foo/bar/my.txt", returns "my.txt".
StringData getBaseName(StringData path) {
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos)
        return path;
    return path.substr(lastSlash + 1);
}

struct FrameInfo {
    void* address;
    void* baseAddress;
    StringData symbol;
    StringData filename = kUnknownFileName;
    ptrdiff_t offset = kOffsetUnknown;
};

class IterationIface {
public:
    virtual ~IterationIface() = default;
    virtual void start() = 0;
    virtual bool done() const = 0;
    virtual const FrameInfo& deref() const = 0;
    virtual void advance() = 0;
};

/**
 * Prints a stack backtrace for the current thread to the specified ostream.
 *
 * The format of the backtrace is:
 *
 * ----- BEGIN BACKTRACE -----
 * JSON backtrace
 * Human-readable backtrace
 * -----  END BACKTRACE  -----
 *
 * The JSON backtrace will be a JSON object with a "backtrace" field, and optionally others.
 * The "backtrace" field is an array, whose elements are frame objects.  A frame object has a
 * "b" field, which is the base-address of the library or executable containing the symbol, and
 * an "o" field, which is the offset into said library or executable of the symbol.
 *
 * The JSON backtrace may optionally contain additional information useful to a backtrace
 * analysis tool.  For example, on Linux it contains a subobject named "somap", describing
 * the objects referenced in the "b" fields of the "backtrace" list.
 */

void printStackTraceGeneric(IterationIface& source, std::ostream& os) {
    // Notes for future refinements: we have a goal of making this malloc-free so it's
    // signal-safe. This huge stack-allocated structure reduces the need for malloc, at the
    // risk of a stack overflow while trying to print a stack trace, which would make life
    // very hard for us when we're debugging crashes for customers. It would also be bad to
    // crash from stack overflow when printing backtraces on demand (SERVER-33445).
    os << std::hex << std::uppercase;
    auto sg = makeGuard([&] { os << std::dec << std::nouppercase; });

    for (source.start(); !source.done(); source.advance()) {
        const auto& f = source.deref();
        os << ' ' << f.address;
    }

    // Display the JSON backtrace
    os << "\n----- BEGIN BACKTRACE -----\n";
    os << R"({"backtrace":[)";
    StringData sep;
    for (source.start(); !source.done(); source.advance()) {
        const auto& f = source.deref();
        os << sep;
        sep = ","_sd;
        os << R"({)";
        os << R"("b":")" << f.baseAddress;
        os << R"(","o":")" << f.offset;
        if (!f.symbol.empty()) {
            os << R"(","s":")" << f.symbol;
        }
        os << R"("})";
    }
    os << ']';

    if (withProcessInfo) {
        if (auto&& pi = getSoMapJson())
            os << R"(,"processInfo":)" << *pi;
    }

    os << "}\n";

    // Display the human-readable trace
    for (source.start(); !source.done(); source.advance()) {
        const auto& f = source.deref();
        os << ' ' << getBaseName(f.filename);
        if (f.baseAddress) {
            os << '(';
            if (!f.symbol.empty()) {
                os << f.symbol;
            }
            if (f.offset != kOffsetUnknown) {
                os << "+0x" << f.offset;
            }
            os << ')';
        }
        os << " [0x" << reinterpret_cast<uintptr_t>(f.address) << ']' << std::endl;
    }
    os << "-----  END BACKTRACE  -----" << std::endl;
}

Dl_info getSymbolInfo(void* addr) {
    Dl_info ret;
    if (!dladdr(addr, &ret)) {
        ret.dli_fname = kUnknownFileName;
        ret.dli_fbase = nullptr;
        ret.dli_sname = nullptr;
        ret.dli_saddr = nullptr;
    }
    return ret;
}

////////////////////////////////////////////////////////////////////////////////
#if defined(MONGO_USE_LIBUNWIND)
namespace impl_unwind {

class State : public IterationIface {
public:
    explicit State(std::ostream& os, bool fromSignal) : _os(os), _fromSignal(fromSignal) {
        if (int r = unw_getcontext(&_context); r < 0) {
            _os << "unw_getcontext: " << unw_strerror(r) << std::endl;
            _failed = true;
        }
    }

private:
    void start() override {
        _f = {};
        _end = false;

        if (_failed) {
            _end = true;
            return;
        }
        int r = unw_init_local2(&_cursor, &_context, _fromSignal ? UNW_INIT_SIGNAL_FRAME : 0);
        if (r < 0) {
            _os << "unw_init_local2: " << unw_strerror(r) << std::endl;
            _end = true;
            return;
        }
        _load();
    }

    bool done() const override { return _end; }

    const FrameInfo& deref() const override { return _f; }

    void advance() override {
        int r = unw_step(&_cursor);
        if (r <= 0) {
            if (r < 0) {
                _os << "error: unw_step: " << unw_strerror(r) << std::endl;
            }
            _end = true;
        }
        if (!_end) {
            _load();
        }
    }

    void _load() {
        _f = {};
        unw_word_t pc;
        if (int r = unw_get_reg(&_cursor, UNW_REG_IP, &pc); r < 0) {
            _os << "unw_get_reg: " << unw_strerror(r) << std::endl;
            _end = true;
            return;
        }
        if (pc == 0) {
            _end = true;
            return;
        }
        _f.address = reinterpret_cast<void*>(pc);
        unw_word_t offset;
        if (int r = unw_get_proc_name(&_cursor, _symbolBuf, sizeof(_symbolBuf), &offset);
            r < 0) {
            _os << "unw_get_proc_name(" << _f.address << "): " << unw_strerror(r)
                    << std::endl;
        } else {
            _f.symbol = _symbolBuf;
            _f.offset = static_cast<ptrdiff_t>(offset);
        }
        {
            Dl_info dli = getSymbolInfo(_f.address);
            _f.baseAddress = dli.dli_fbase;
            if (_f.offset == kOffsetUnknown) {
                _f.offset =
                    static_cast<char*>(_f.address) - static_cast<char*>(dli.dli_saddr);
            }
            if (dli.dli_fbase)
                _f.filename = dli.dli_fname;
            // Don't overwrite _f->symbol if we already figured out the symbol name.
            if (dli.dli_sname && _f.symbol.empty()) {
                _f.symbol = dli.dli_sname;
            }
        }
    }

    std::ostream& _os;
    bool _fromSignal;

    FrameInfo _f{};

    bool _failed = false;
    bool _end = false;

    unw_context_t _context;
    unw_cursor_t _cursor;

    char _symbolBuf[kSymbolMax];
};

void printStackTraceInternal(std::ostream& os, bool fromSignal) {
    State state(os, fromSignal);
    printStackTraceGeneric(state, os);
}

}  // namespace impl_unwind
#endif  // MONGO_USE_LIBUNWIND

////////////////////////////////////////////////////////////////////////////////
#if defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
namespace impl_execinfo {

class State : public IterationIface {
public:
    explicit State(std::ostream& os) {
        _n = ::backtrace(_addresses.data(), _addresses.size());
        if (_n == 0) {
            int err = errno;
            os << "Unable to collect backtrace addresses (errno: "
               << err << ' ' << strerror(err) << ')' << std::endl;
            return;
        }
    }

private:
    void start() override {
        _i = 0;
        if (!done())
            _load();
    }
    bool done() const override {
        return _i >= _n;
    }
    const FrameInfo& deref() const override {
        return _f;
    }
    void advance() override {
        ++_i;
        if (!done())
            _load();
    }

    void _load() {
        _f = {};
        _f.address = _addresses[_i];
        Dl_info dli = getSymbolInfo(_f.address);
        _f.baseAddress = dli.dli_fbase;
        _f.symbol = dli.dli_sname;
        _f.offset = reinterpret_cast<uintptr_t>(_f.address) -
            uintptr_t(_f.symbol.empty() ? _f.baseAddress : dli.dli_saddr);
        if (dli.dli_fbase) {
            _f.filename = dli.dli_fname;
        }
    }

    std::array<void*, kFrameMax> _addresses;
    size_t _n = 0;
    size_t _i = 0;
    FrameInfo _f;
};

void printStackTraceInternal(std::ostream& os, bool fromSignal) {
    State state(os);
    printStackTraceGeneric(state, os);
}

}  // namespace impl_execinfo
#endif  // defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)

////////////////////////////////////////////////////////////////////////////////
namespace impl_none {
void printStackTraceInternal(std::ostream& os, bool fromSignal) {
    os << "This platform does not support printing stacktraces" << std::endl;
}
}  // namespace impl_none

#if defined(MONGO_USE_LIBUNWIND)
namespace impl = impl_unwind;
#elif defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
namespace impl = impl_execinfo;
#else
namespace impl = impl_none;
#endif

}  // namespace stacktrace_detail
}  // namespace

void printStackTrace(std::ostream& os) {
    stacktrace_detail::impl::printStackTraceInternal(os, false);
}

void printStackTraceFromSignal(std::ostream& os) {
    stacktrace_detail::impl::printStackTraceInternal(os, true);
}

}  // namespace mongo
