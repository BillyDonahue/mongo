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

#include <climits>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "mongo/base/init.h"
#include "mongo/config.h"
#include "mongo/db/jsobj.h"
#include "mongo/util/hex.h"
#include "mongo/util/log.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/str.h"
#include "mongo/util/version.h"

#if defined(__linux__)
#include <elf.h>
#include <link.h>
#endif

#if defined(__APPLE__) && defined(__MACH__)
#include <mach-o/dyld.h>
#include <mach-o/ldsyms.h>
#include <mach-o/loader.h>
#endif

#if defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
#include <execinfo.h>
#endif
#if defined(MONGO_USE_LIBUNWIND)
#include <libunwind.h>
#endif

namespace mongo {

namespace stacktrace_detail {

constexpr bool withProcessInfo = false;

////////////////////////////////////////////////////////////////////////////////
namespace so_map {

#if defined(__linux__)

/**
 * Processes an ELF Phdr for a NOTE segment, updating "soInfo".
 *
 * Looks for the GNU Build ID NOTE, and adds a buildId field to soInfo if it finds one.
 */
void processNoteSegment(const dl_phdr_info& info, const ElfW(Phdr) & phdr, BSONObjBuilder* soInfo) {
#ifdef NT_GNU_BUILD_ID
    const char* const notesBegin = reinterpret_cast<const char*>(info.dlpi_addr) + phdr.p_vaddr;
    const char* const notesEnd = notesBegin + phdr.p_memsz;
    ElfW(Nhdr) noteHeader;
    // Returns the size in bytes of an ELF note entry with the given header.
    auto roundUpToElfWordAlignment = [](size_t offset) -> size_t {
        static const size_t elfWordSizeBytes = sizeof(ElfW(Word));
        return (offset + (elfWordSizeBytes - 1)) & ~(elfWordSizeBytes - 1);
    };
    auto getNoteSizeBytes = [&](const ElfW(Nhdr) & noteHeader) -> size_t {
        return sizeof(noteHeader) + roundUpToElfWordAlignment(noteHeader.n_namesz) +
            roundUpToElfWordAlignment(noteHeader.n_descsz);
    };
    for (const char* notesCurr = notesBegin; (notesCurr + sizeof(noteHeader)) < notesEnd;
         notesCurr += getNoteSizeBytes(noteHeader)) {
        memcpy(&noteHeader, notesCurr, sizeof(noteHeader));
        if (noteHeader.n_type != NT_GNU_BUILD_ID)
            continue;
        const char* const noteNameBegin = notesCurr + sizeof(noteHeader);
        if (StringData(noteNameBegin, noteHeader.n_namesz - 1) != ELF_NOTE_GNU) {
            continue;
        }
        const char* const noteDescBegin =
            noteNameBegin + roundUpToElfWordAlignment(noteHeader.n_namesz);
        soInfo->append("buildId", toHex(noteDescBegin, noteHeader.n_descsz));
    }
#endif
}

/**
 * Processes an ELF Phdr for a LOAD segment, updating "soInfo".
 *
 * The goal of this operation is to find out if the current object is an executable or a shared
 * object, by looking for the LOAD segment that maps the first several bytes of the file (the
 * ELF header).  If it's an executable, this method updates soInfo with the load address of the
 * segment
 */
void processLoadSegment(const dl_phdr_info& info, const ElfW(Phdr) & phdr, BSONObjBuilder* soInfo) {
    if (phdr.p_offset)
        return;
    if (phdr.p_memsz < sizeof(ElfW(Ehdr)))
        return;

    // Segment includes beginning of file and is large enough to hold the ELF header
    ElfW(Ehdr) eHeader;
    memcpy(&eHeader, reinterpret_cast<const char*>(info.dlpi_addr) + phdr.p_vaddr, sizeof(eHeader));

    std::string quotedFileName = "\"" + str::escape(info.dlpi_name) + "\"";

    if (memcmp(&eHeader.e_ident[0], ELFMAG, SELFMAG)) {
        warning() << "Bad ELF magic number in image of " << quotedFileName;
        return;
    }

#if defined(__ELF_NATIVE_CLASS)
#define ARCH_BITS __ELF_NATIVE_CLASS
#else  //__ELF_NATIVE_CLASS
#if defined(__x86_64__) || defined(__aarch64__)
#define ARCH_BITS 64
#elif defined(__arm__)
#define ARCH_BITS 32
#else
#error Unknown target architecture.
#endif  //__aarch64__
#endif  //__ELF_NATIVE_CLASS
#define PPCAT_(a, b) a##b
#define MKELFCLASS(N) PPCAT_(ELFCLASS, N)
    static constexpr int kArchBits = ARCH_BITS;
    if (eHeader.e_ident[EI_CLASS] != MKELFCLASS(ARCH_BITS)) {
        warning() << "Expected elf file class of " << quotedFileName << " to be "
                  << MKELFCLASS(ARCH_BITS) << "(" << kArchBits << "-bit), but found "
                  << int(eHeader.e_ident[4]);
        return;
    }
#undef ARCH_BITS

    if (eHeader.e_ident[EI_VERSION] != EV_CURRENT) {
        warning() << "Wrong ELF version in " << quotedFileName << ".  Expected " << EV_CURRENT
                  << " but found " << int(eHeader.e_ident[EI_VERSION]);
        return;
    }

    soInfo->append("elfType", eHeader.e_type);

    switch (eHeader.e_type) {
        case ET_EXEC:
            break;
        case ET_DYN:
            return;
        default:
            warning() << "Surprised to find " << quotedFileName << " is ELF file of type "
                      << eHeader.e_type;
            return;
    }

    soInfo->append("b", integerToHex(phdr.p_vaddr));
}

/**
 * Callback that processes an ELF object linked into the current address space.
 *
 * Used by dl_iterate_phdr in ExtractSOMap, below, to build up the list of linked
 * objects.
 *
 * Each entry built by an invocation of ths function may have the following fields:
 * * "b", the base address at which an object is loaded.
 * * "path", the path on the file system to the object.
 * * "buildId", the GNU Build ID of the object.
 * * "elfType", the ELF type of the object, typically 2 or 3 (executable or SO).
 *
 * At post-processing time, the buildId field can be used to identify the file containing
 * debug symbols for objects loaded at the given "laodAddr", which in turn can be used with
 * the "backtrace" displayed in printStackTrace to get detailed unwind information.
 */
int outputSOInfo(dl_phdr_info* info, size_t sz, void* data) {
    auto isSegmentMappedReadable = [](const ElfW(Phdr) & phdr) -> bool {
        return phdr.p_flags & PF_R;
    };
    BSONObjBuilder soInfo(reinterpret_cast<BSONArrayBuilder*>(data)->subobjStart());
    if (info->dlpi_addr)
        soInfo.append("b", integerToHex(ElfW(Addr)(info->dlpi_addr)));
    if (info->dlpi_name && *info->dlpi_name)
        soInfo.append("path", info->dlpi_name);

    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) & phdr(info->dlpi_phdr[i]);
        if (!isSegmentMappedReadable(phdr))
            continue;
        switch (phdr.p_type) {
            case PT_NOTE:
                processNoteSegment(*info, phdr, &soInfo);
                break;
            case PT_LOAD:
                processLoadSegment(*info, phdr, &soInfo);
                break;
            default:
                break;
        }
    }
    return 0;
}

void addOSComponentsToSoMap(BSONObjBuilder* soMap) {
    BSONArrayBuilder soList(soMap->subarrayStart("somap"));
    dl_iterate_phdr(outputSOInfo, &soList);
    soList.done();
}

#elif defined(__APPLE__) && defined(__MACH__)

void addOSComponentsToSoMap(BSONObjBuilder* soMap) {
    auto lcNext = [](const char* lcCurr) -> const char* {
        return lcCurr + reinterpret_cast<const load_command*>(lcCurr)->cmdsize;
    };
    auto lcType = [](const char* lcCurr) -> uint32_t {
        return reinterpret_cast<const load_command*>(lcCurr)->cmd;
    };
    auto maybeAppendLoadAddr = [](BSONObjBuilder* soInfo, const auto* segmentCommand) -> bool {
        if (StringData(SEG_TEXT) != segmentCommand->segname) {
            return false;
        }
        *soInfo << "vmaddr" << integerToHex(segmentCommand->vmaddr);
        return true;
    };
    const uint32_t numImages = _dyld_image_count();
    BSONArrayBuilder soList(soMap->subarrayStart("somap"));
    for (uint32_t i = 0; i < numImages; ++i) {
        BSONObjBuilder soInfo(soList.subobjStart());
        const char* name = _dyld_get_image_name(i);
        if (name)
            soInfo << "path" << name;
        const mach_header* header = _dyld_get_image_header(i);
        if (!header)
            continue;
        size_t headerSize;
        if (header->magic == MH_MAGIC) {
            headerSize = sizeof(mach_header);
        } else if (header->magic == MH_MAGIC_64) {
            headerSize = sizeof(mach_header_64);
        } else {
            continue;
        }
        soInfo << "machType" << static_cast<int32_t>(header->filetype);
        soInfo << "b" << integerToHex(reinterpret_cast<intptr_t>(header));
        const char* const loadCommandsBegin = reinterpret_cast<const char*>(header) + headerSize;
        const char* const loadCommandsEnd = loadCommandsBegin + header->sizeofcmds;
        // Search the "load command" data in the Mach object for the entry encoding the UUID of the
        // object, and for the __TEXT segment. Adding the "vmaddr" field of the __TEXT segment load
        // command of an executable or dylib to an offset in that library provides an address
        // suitable to passing to atos or llvm-symbolizer for symbolization.
        //
        // See, for example, http://lldb.llvm.org/symbolication.html.
        bool foundTextSegment = false;
        for (const char* lcCurr = loadCommandsBegin; lcCurr < loadCommandsEnd;
             lcCurr = lcNext(lcCurr)) {
            switch (lcType(lcCurr)) {
                case LC_UUID: {
                    const auto uuidCmd = reinterpret_cast<const uuid_command*>(lcCurr);
                    soInfo << "buildId" << toHex(uuidCmd->uuid, 16);
                    break;
                }
                case LC_SEGMENT_64:
                    if (!foundTextSegment) {
                        foundTextSegment = maybeAppendLoadAddr(
                            &soInfo, reinterpret_cast<const segment_command_64*>(lcCurr));
                    }
                    break;
                case LC_SEGMENT:
                    if (!foundTextSegment) {
                        foundTextSegment = maybeAppendLoadAddr(
                            &soInfo, reinterpret_cast<const segment_command*>(lcCurr));
                    }
                    break;
            }
        }
    }
}

#else  // unknown OS

void addOSComponentsToSoMap(BSONObjBuilder* soMap) {}

#endif  // unknown OS

// Optional string containing extra unwinding information in JSON form.
const std::string* soMapJson = nullptr;

/**
 * Builds the "soMapJson" string, which is a JSON encoding of various pieces of information
 * about a running process, including the map from load addresses to shared objects loaded at
 * those addresses.
 */
MONGO_INITIALIZER(ExtractSOMap)(InitializerContext*) {
    BSONObjBuilder soMap;

    auto&& vii = VersionInfoInterface::instance(VersionInfoInterface::NotEnabledAction::kFallback);
    soMap << "mongodbVersion" << vii.version();
    soMap << "gitVersion" << vii.gitVersion();
    soMap << "compiledModules" << vii.modules();

    struct utsname unameData;
    if (!uname(&unameData)) {
        BSONObjBuilder unameBuilder(soMap.subobjStart("uname"));
        unameBuilder << "sysname" << unameData.sysname << "release" << unameData.release
                     << "version" << unameData.version << "machine" << unameData.machine;
    }
    addOSComponentsToSoMap(&soMap);
    soMapJson = new std::string(soMap.done().jsonString(Strict));
    return Status::OK();
}

const std::string* getSoMapJson() {
    return soMapJson;
}

}  // namespace so_map
////////////////////////////////////////////////////////////////////////////////

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

struct Frame {
    void* address;
    void* baseAddress;
    StringData symbol;
    StringData filename = kUnknownFileName;
    ptrdiff_t offset = kOffsetUnknown;
};

struct IterEnd {};

template <typename Ops>
struct Iter {
    Ops ops;

    Iter& operator++() {
        ops.advance();
        return *this;
    }
    const Frame* operator->() const {
        return &ops.deref();
    }
    const Frame& operator*() const {
        return ops.deref();
    }
    bool operator!=(IterEnd) const {
        return !ops.done();
    }
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
template <typename State>
void printStackTraceGeneric(State& state, StackTraceSink& os) {
    // Maintainers: keep this signal-safe.
    for (const auto& f : state) {
        os << " " << f.address;
    }

    // Display the JSON backtrace
    os << "\n----- BEGIN BACKTRACE -----\n";
    os << R"({"backtrace":[)";
    StringData sep;
    for (const auto& f : state) {
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
    os << "]";

    if (withProcessInfo) {
        if (auto&& pi = so_map::getSoMapJson())
            os << R"(,"processInfo":)" << *pi;
    }

    os << "}\n";

    // Display the human-readable trace
    for (const auto& f : state) {
        os << " " << getBaseName(f.filename);
        if (f.baseAddress) {
            os << "(";
            if (!f.symbol.empty()) {
                os << f.symbol;
            }
            if (f.offset != kOffsetUnknown) {
                os << "+0x" << f.offset;
            }
            os << ")";
        }
        os << " [0x" << reinterpret_cast<uintptr_t>(f.address) << "]\n";
    }
    os << "-----  END BACKTRACE  -----\n";
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

class State {
private:
    class IterOps;

public:
    explicit State(StackTraceSink& os, bool fromSignal) : _os(os), _fromSignal(fromSignal) {}

    void startTrace() {
        if (int r = unw_getcontext(&_context); r < 0) {
            _os << "unw_getcontext: " << unw_strerror(r) << "\n";
            _failed = true;
        }
    }

    Iter<IterOps> begin();

    IterEnd end() const {
        return {};
    }

    // Initialize cursor to current frame for local unwinding.
    unw_context_t _context;
    StackTraceSink& _os;
    bool _fromSignal;
    bool _failed = false;
};

class State::IterOps {
public:
    explicit IterOps(State* s) : _s{s} {
        if (_s->_failed) {
            _end = true;
            return;
        }
        int r =
            unw_init_local2(&_cursor, &_s->_context, _s->_fromSignal ? UNW_INIT_SIGNAL_FRAME : 0);
        if (r < 0) {
            _s->_os << "unw_init_local2: " << unw_strerror(r) << "\n";
            _end = true;
            return;
        }
        _load();
    }

    bool done() const {
        return _end;
    }

    const Frame& deref() const {
        return _f;
    }

    void advance() {
        int r = unw_step(&_cursor);
        if (r <= 0) {
            if (r < 0) {
                _s->_os << "error: unw_step: " << unw_strerror(r) << "\n";
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
            _s->_os << "unw_get_reg: " << unw_strerror(r) << "\n";
            _end = true;
            return;
        }
        if (pc == 0) {
            _end = true;
            return;
        }
        _f.address = reinterpret_cast<void*>(pc);
        unw_word_t offset;
        if (int r = unw_get_proc_name(&_cursor, _symbolBuf, sizeof(_symbolBuf), &offset); r < 0) {
            _s->_os << "unw_get_proc_name(" << _f.address << "): " << unw_strerror(r) << "\n";
        } else {
            _f.symbol = _symbolBuf;
            _f.offset = static_cast<ptrdiff_t>(offset);
        }
        {
            Dl_info dli = getSymbolInfo(_f.address);
            _f.baseAddress = dli.dli_fbase;
            if (_f.offset == kOffsetUnknown) {
                _f.offset = static_cast<char*>(_f.address) - static_cast<char*>(dli.dli_saddr);
            }
            if (dli.dli_fbase)
                _f.filename = dli.dli_fname;
            // Don't overwrite _f->symbol if we already figured out the symbol name.
            if (dli.dli_sname && _f.symbol.empty()) {
                _f.symbol = dli.dli_sname;
            }
        }
    }

    State* _s;
    unw_cursor_t _cursor;
    bool _end = false;
    Frame _f{};
    char _symbolBuf[kSymbolMax];
};

auto State::begin() -> Iter<IterOps> {
    return Iter<IterOps>{IterOps(this)};
}

void printStackTraceInternal(StackTraceSink& os, bool fromSignal) {
    State state(os, fromSignal);
    state.startTrace();
    printStackTraceGeneric(state, os);
}

}  // namespace impl_unwind
#endif  // MONGO_USE_LIBUNWIND

////////////////////////////////////////////////////////////////////////////////
#if defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
namespace impl_execinfo {

class State {
private:
    class IterOps;

public:
    explicit State(StackTraceSink& os) : _os(os) {}

    void startTrace() {
        _n = ::backtrace(_addresses, kFrameMax);
        if (_n == 0) {
            // strerror is not signal-safe
            int errnoSaved = errno;
            _os << "Unable to collect backtrace addresses (errno: " << errnoSaved << ")\n";
            return;
        }
    }

    Iter<IterOps> begin();

    IterEnd end() const {
        return {};
    }

private:
    StackTraceSink& _os;
    size_t _n{};
    void* _addresses[kFrameMax];
};

class State::IterOps {
public:
    explicit IterOps(const State* s) : _s(s) {
        if (!done())
            _load();
    }

    bool done() const {
        return _i >= _s->_n;
    }

    const Frame& deref() const {
        return _f;
    }

    void advance() {
        ++_i;
        if (!done())
            _load();
    }

private:
    void _load() {
        _f = {};
        _f.address = _s->_addresses[_i];
        Dl_info dli = getSymbolInfo(_f.address);
        _f.baseAddress = dli.dli_fbase;
        _f.symbol = dli.dli_sname;
        _f.offset = reinterpret_cast<uintptr_t>(_f.address) -
            uintptr_t(_f.symbol.empty() ? _f.baseAddress : dli.dli_saddr);
        if (dli.dli_fbase) {
            _f.filename = dli.dli_fname;
        }
    }

    const State* _s;
    size_t _i{};
    Frame _f{};
};

auto State::begin() -> Iter<IterOps> {
    return Iter<IterOps>{IterOps(this)};
}

void printStackTraceInternal(StackTraceSink& os, bool fromSignal) {
    State state(os);
    state.startTrace();
    printStackTraceGeneric(state, os);
}

}  // namespace impl_execinfo
#endif  // defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)

////////////////////////////////////////////////////////////////////////////////
namespace impl_none {
void printStackTraceInternal(StackTraceSink& os, bool fromSignal) {
    os << "This platform does not support printing stacktraces\n";
}
}  // namespace impl_none

#if defined(MONGO_USE_LIBUNWIND)
namespace impl = impl_unwind;
#elif defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
namespace impl = impl_execinfo;
#else
namespace impl = impl_none;
#endif

void printStackTraceInternal(StackTraceSink& sink, bool fromSignal) {
    impl::printStackTraceInternal(sink, fromSignal);
}

void printStackTraceInternal(std::ostream& os, bool fromSignal) {
    static const bool kExperimental = false;  // TODO: Just trying this out
    if (kExperimental) {
        static constexpr size_t kBufSize = size_t{1} << 10;
        auto buf = std::make_unique<char[]>(kBufSize);
        MemoryBlockStackTraceSink memSink(buf.get(), kBufSize);
        printStackTraceInternal(memSink, fromSignal);
        os << memSink.data();
        if (memSink.data().size() < memSink.written()) {
            os << " ... (+" << (memSink.written() - memSink.data().size()) << " more bytes)";
        }
        return;
    }

    TempFileStackTraceSink sink;
    printStackTraceInternal(sink, fromSignal);
    sink.rewind();
    while (sink.good()) {
        char buf[256];
        StringData str = sink.read(buf, sizeof(buf));
        if (str.empty()) break;
        os << str;
    }
    sink.close();

    if (!fromSignal) {
        const auto& f = sink.failure();
        if (f.errnoSaved != 0) {
            std::cerr << f.failedOp << ":" << strerror(f.errnoSaved);
        }
    }
}

}  // namespace stacktrace_detail

void printStackTrace(StackTraceSink& sink) {
    stacktrace_detail::printStackTraceInternal(sink, false);
}
void printStackTraceFromSignal(StackTraceSink& sink) {
    stacktrace_detail::printStackTraceInternal(sink, true);
}

void printStackTrace(std::ostream& os) {
    stacktrace_detail::printStackTraceInternal(os, false);
}

void printStackTraceFromSignal(std::ostream& os) {
    stacktrace_detail::printStackTraceInternal(os, true);
}


TempFileStackTraceSink::TempFileStackTraceSink(const char* mktempTemplate) {
    strncpy(_filename, mktempTemplate, sizeof(_filename));
    _fd = mkstemp(_filename);
    if (_fd < 0) {
        _failure = {"mkstemp"_sd, errno};
    }
}

TempFileStackTraceSink::~TempFileStackTraceSink() {
    if (_fd != -1) {
        close();
    }
    if (int r = unlink(_filename); r < 0) {
        _failure = {"unlink"_sd, errno};
    }
}

bool TempFileStackTraceSink::good() const {
    return _fd >= 0 && _failure.errnoSaved == 0;
}

void TempFileStackTraceSink::rewind() {
    if (lseek(_fd, 0, SEEK_SET) == off_t(-1)) {
        _failure = {"lseek"_sd, errno};
        return;
    }
}

void TempFileStackTraceSink::close() {
    if (::close(_fd) < 0) {
        _failure = {"close"_sd, errno};
    }
    _fd = -1;
}

StringData TempFileStackTraceSink::read(char* buf, size_t bufsz) {
    ssize_t n;
    do {
        n = ::read(_fd, buf, bufsz);
    } while ((n == -1) && (errno == EINTR));
    if (n == -1) {
        _failure = {"read"_sd, errno};
        return StringData();
    }
    return StringData(buf, n);
}

void TempFileStackTraceSink::doWrite(StringData s) {
    if (!good())
        return;
    while (!s.empty()) {
        ssize_t n;
        do {
            n = ::write(_fd, s.rawData(), s.size());
        } while ((n == -1) & (errno == EINTR));
        if (n == -1) {
            _failure = {"write"_sd, errno};
            return;
        }
        s = s.substr(n);
    }
}

}  // namespace mongo
