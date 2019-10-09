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
#include <boost/optional.hpp>
#include <climits>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

#include "mongo/base/init.h"
#include "mongo/config.h"
#include "mongo/platform/compiler_gcc.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/stacktrace_json.h"
#include "mongo/util/version.h"

namespace mongo::stack_trace {
namespace {

constexpr size_t kSymbolMax = 512;

class IterationIface {
public:
    enum Flags {
        kRaw = 0,
        kSymbolic = 1,  // Also gather symbolic metadata.
    };

    virtual ~IterationIface() = default;
    virtual void start(Flags f) = 0;
    virtual bool done() const = 0;
    virtual const AddressMetadata& deref() const = 0;
    virtual void advance() = 0;
};

/**
 * Iterates through the stacktrace to extract the bases addresses for each address in the
 * stacktrace. Returns a sorted, unique sequence of these base addresses.
 */
ArrayAndSize<uint64_t, kFrameMax> uniqueBases(IterationIface& source) {
    ArrayAndSize<uint64_t, kFrameMax> bases;
    for (source.start(source.kSymbolic); !source.done(); source.advance()) {
        const auto& f = source.deref();
        if (f.soFile) {
            uintptr_t base = f.soFile->base;
            // Push base into bases keeping it sorted and unique.
            auto ins = std::lower_bound(bases.begin(), bases.end(), base);
            if (ins != bases.end() && *ins == base) {
                continue;
            } else {
                bases.push_back(base);
                std::rotate(ins, bases.end() - 1, bases.end());
            }
        }
    }
    return bases;
}

void printRawAddrsLine(IterationIface& source, Sink& sink) {
    for (source.start(source.kRaw); !source.done(); source.advance()) {
        sink << " " << Hex(source.deref().address).str();
    }
}

void appendJsonBacktrace(IterationIface& source, CheapJson::Value& jsonRoot) {
    CheapJson::Value frames = jsonRoot.appendKey("backtrace").appendArr();
    for (source.start(source.kSymbolic); !source.done(); source.advance()) {
        const auto& f = source.deref();
        uint64_t base = f.soFile ? f.soFile->base : 0;
        CheapJson::Value frameObj = frames.appendObj();
        frameObj.appendKey("b").append(Hex(base).str());
        frameObj.appendKey("o").append(Hex(f.address - base).str());
        if (f.symbol) {
            frameObj.appendKey("s").append(f.symbol->name);
        }
    }
}

/**
 * Most elements of `bsonProcInfo` are copied verbatim into the `jsonProcInfo` Json
 * object. But if `bases` is non-null, The "somap" BSON Array is filtered to only
 * include elements corresponding to the addresses in `bases`.
 */
void printJsonProcessInfoCommon(const BSONObj& bsonProcInfo,
                                CheapJson::Value& jsonProcInfo,
                                const ArrayAndSize<uint64_t, kFrameMax>* bases) {
    for (const BSONElement& be : bsonProcInfo) {
        if (bases && be.type() == BSONType::Array) {
            if (StringData key = be.fieldNameStringData(); key == "somap") {
                CheapJson::Value jsonArr = jsonProcInfo.appendKey(key).appendArr();
                for (const BSONElement& ae : be.Array()) {
                    BSONObj bRec = ae.embeddedObject();
                    uint64_t soBase = Hex::fromHex(bRec.getStringField("b"));
                    if (std::binary_search(bases->begin(), bases->end(), soBase))
                        jsonArr.append(ae);
                }
                continue;
            }
        }
        jsonProcInfo.append(be);
    }
}

void printJsonProcessInfoTrimmed(IterationIface& iteration,
                                 const BSONObj& bsonProcInfo,
                                 CheapJson::Value& jsonProcInfo) {
    auto bases = uniqueBases(iteration);
    printJsonProcessInfoCommon(bsonProcInfo, jsonProcInfo, &bases);
}

void appendJsonProcessInfo(const Tracer::Options& options,
                           IterationIface& source,
                           CheapJson::Value& jsonRoot) {
    if (!options.withProcessInfo)
        return;
    auto bsonSoMap = globalSharedObjectMapInfo();
    if (!bsonSoMap)
        return;
    const BSONObj& bsonProcInfo = bsonSoMap->obj();
    CheapJson::Value jsonProcInfo = jsonRoot.appendKey("processInfo").appendObj();
    if (options.trimSoMap) {
        printJsonProcessInfoTrimmed(source, bsonProcInfo, jsonProcInfo);
    } else {
        printJsonProcessInfoCommon(bsonProcInfo, jsonProcInfo, nullptr);
    }
}

void printHumanReadable(IterationIface& iteration, Sink& sink) {
    for (iteration.start(iteration.kSymbolic); !iteration.done(); iteration.advance()) {
        printOneMetadata(iteration.deref(), sink);
    }
}

/**
 * Prints a stack backtrace for the current thread to the specified sink.
 *
 * The format of the backtrace is:
 *
 *     hexAddresses ...                    // space-separated
 *     ----- BEGIN BACKTRACE -----
 *     {backtrace:..., processInfo:...}    // json
 *     Human-readable backtrace
 *     -----  END BACKTRACE  -----
 *
 * The JSON backtrace will be a JSON object with a "backtrace" field, and optionally others.
 * The "backtrace" field is an array, whose elements are frame objects.  A frame object has a
 * "b" field, which is the base-address of the library or executable containing the symbol, and
 * an "o" field, which is the offset into said library or executable of the symbol.
 *
 * The JSON backtrace may optionally contain additional information useful to a backtrace
 * analysis tool.  For example, on Linux it contains a subobject named "somap", describing
 * the objects referenced in the "b" fields of the "backtrace" list.
 *
 * TODO(SERVER-42670): make this asynchronous signal safe.
 */
void printStackTraceGeneric(const Tracer::Options& options, IterationIface& iteration, Sink& sink) {
    printRawAddrsLine(iteration, sink);
    sink << "\n----- BEGIN BACKTRACE -----\n";
    {
        CheapJson json{sink};
        CheapJson::Value doc = json.doc();
        CheapJson::Value jsonRootObj = doc.appendObj();
        appendJsonBacktrace(iteration, jsonRootObj);
        appendJsonProcessInfo(options, iteration, jsonRootObj);
    }
    sink << "\n";
    if (options.withHumanReadable) {
        printHumanReadable(iteration, sink);
    }
    sink << "-----  END BACKTRACE  -----\n";
}

#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND

class Iteration : public IterationIface {
public:
    explicit Iteration(Context& context, const Tracer::Options& options)
        : _context(context), _options(options) {
        if (int r = context.unwError; r < 0) {
            _errSink() << "unw_getcontext: " << unw_strerror(r) << "\n";
            _failed = true;
        }
    }

    void start(Flags f) override {
        _flags = f;
        _end = false;

        if (_failed) {
            _end = true;
            return;
        }
        if (int r = unw_init_local(&_cursor, &context()); r < 0) {
            _errSink() << "unw_init_local: " << unw_strerror(r) << "\n";
            _end = true;
            return;
        }
        _load();
    }

    bool done() const override {
        return _end;
    }

    const AddressMetadata& deref() const override {
        return _f;
    }

    void advance() override {
        int r = unw_step(&_cursor);
        if (r <= 0) {
            if (r < 0) {
                _errSink() << "error: unw_step: " << unw_strerror(r) << "\n";
            }
            _end = true;
        }
        if (!_end) {
            _load();
        }
    }

private:
    void _load() {
        _f = {};
        unw_word_t pc;
        if (int r = unw_get_reg(&_cursor, UNW_REG_IP, &pc); r < 0) {
            _errSink() << "unw_get_reg: " << unw_strerror(r) << "\n";
            _end = true;
            return;
        }
        if (pc == 0) {
            _end = true;
            return;
        }
        _f.address = pc;
        if (_flags & kSymbolic) {
            unw_word_t offset;
            if (int r = unw_get_proc_name(&_cursor, _symbolBuf, sizeof(_symbolBuf), &offset);
                r < 0) {
            } else {
                _f.symbol = AddressMetadata::NameBase{_symbolBuf, _f.address - offset};
            }
            mergeDlInfo(_f);
        }
    }

    unw_context_t& context() const {
        return _context.unwContext;
    }

    Sink& _errSink() const {
        return *_options.errSink;
    }

    Context& _context;
    const Tracer::Options& _options;
    unw_cursor_t _cursor;
    Flags _flags;
    AddressMetadata _f{};

    bool _failed = false;
    bool _end = false;

    char _symbolBuf[kSymbolMax];
};

#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO

class Iteration : public IterationIface {
public:
    explicit Iteration(Context& context, const Tracer::Options& options)
        : _context(context), _options{options} {}

    void start(Flags f) override {
        _flags = f;
        _i = 0;
        if (!done())
            _load();
    }
    bool done() const override {
        return _i >= _context.addresses.size();
    }
    const AddressMetadata& deref() const override {
        return _f;
    }
    void advance() override {
        ++_i;
        if (!done())
            _load();
    }

private:
    void _load() {
        _f = {};
        _f.address = reinterpret_cast<uintptr_t>(_context.addresses[_i]);
        if (_flags & kSymbolic) {
            mergeDlInfo(_f);
        }
    }

    Flags _flags;
    AddressMetadata _f;

    Context& _context;
    const Tracer::Options& _options;
    size_t _i = 0;
};

#endif  // MONGO_STACKTRACE_BACKEND

}  // namespace

void Tracer::print(Context& context, Sink& sink) const {
#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO
    /** complain about a failed context */
    if (context.addresses.empty()) {
        sink << "Unable to collect backtrace addresses: " << Dec(context.savedErrno).str() << " "
             << strerror(context.savedErrno) << "\n";
        return;
    }
#endif  // MONGO_STACKTRACE_BACKEND
    Iteration iteration(context, options);
    printStackTraceGeneric(options, iteration, sink);
}

size_t Tracer::backtrace(void** addrs, size_t capacity) const {
#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND
    return unw_backtrace(addrs, capacity);
#elif MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_EXECINFO
    return ::backtrace(addrs, capacity);
#endif  // MONGO_STACKTRACE_BACKEND
}

size_t Tracer::backtraceWithMetadata(void** addrs, AddressMetadata* meta, size_t capacity) const {
#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND
    if (!options.dlAddrOnly) {
        // Libunwind can do better than getAddrInfo, by using a `unw_get_proc_name` walk.
        // Need storage for the symbols though!
        Context context;
        MONGO_STACKTRACE_CONTEXT_INITIALIZE(context);
        auto iteration = Iteration(context, options);
        IterationIface& iter = iteration;
        size_t i = 0;
        for (iter.start(iteration.kSymbolic); !iter.done(); iter.advance()) {
            const auto& m = iter.deref();
            if (i == capacity) {
                break;
            }
            addrs[i] = reinterpret_cast<void*>(m.address);
            if (options.alloc) {
                // Fixup meta name fields to be backed by alloc storage.
                meta[i] = m.allocateCopy(*options.alloc);
            }
            ++i;
        }
        return i;
    }
#endif
    size_t n = backtrace(addrs, capacity);
    for (size_t i = 0; i < n; ++i) {
        getAddrInfo(addrs[i], &meta[i]);
    }
    return n;
}

int Tracer::getAddrInfo(void* addr, AddressMetadata* meta) const {
    *meta = {};
    meta->address = reinterpret_cast<uintptr_t>(addr);
    mergeDlInfo(*meta);
    return 0;
}

}  // namespace mongo::stack_trace
