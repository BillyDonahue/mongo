/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "mongo/platform/basic.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <functional>
#include <regex>
#include <signal.h>
#include <sstream>
#include <thread>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/json.h"
#include "mongo/stdx/thread.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/stacktrace.h"
#include "mongo/util/stacktrace_json.h"

/** `sigaltstack` was introduced in glibc-2.12 in 2010. */
#if !defined(_WIN32)
#define HAVE_SIGALTSTACK
#endif

namespace mongo {

namespace stacktrace_test_detail {

struct RecursionParam {
    std::uint64_t n;
    std::function<void()> f;
};

MONGO_COMPILER_NOINLINE int recurseWithLinkage(RecursionParam& p, std::uint64_t i = 0) {
    if (i == p.n) {
        p.f();
    } else {
        recurseWithLinkage(p, i + 1);
    }
    return 0;
}

}  // namespace stacktrace_test_detail

namespace {

using namespace fmt::literals;

constexpr bool kSuperVerbose = 0;  // Devel instrumentation

#if defined(_WIN32)
constexpr bool kIsWindows = true;
#else
constexpr bool kIsWindows = false;
#endif

template <typename T>
static std::string ostr(const T& x) {
    std::ostringstream os;
    os << x;
    return os.str();
}

class StringSink : public stack_trace::Sink {
public:
    StringSink(std::string& s) : _s{s} {}

private:
    void doWrite(StringData v) override {
        format_to(std::back_inserter(_s), FMT_STRING("{}"), v);
    }

    std::string& _s;
};

class CheapJsonTest : public unittest::Test {
public:
    using unittest::Test::Test;
};

TEST_F(CheapJsonTest, Appender) {
    std::string s;
    StringSink sink{s};
    sink << "Hello"
         << ":"
         << "hi";
    ASSERT_EQ(s, "Hello:hi");
}

TEST_F(CheapJsonTest, Hex) {
    using Hex = stack_trace::Hex;
    ASSERT_EQ(Hex(0).str(), "0");
    ASSERT_EQ(Hex(0xffff).str(), "FFFF");
    ASSERT_EQ(Hex(0xfff0).str(), "FFF0");
    ASSERT_EQ(Hex(0x8000'0000'0000'0000).str(), "8000000000000000");
    ASSERT_EQ(Hex::fromHex("FFFF"), 0xffff);
    ASSERT_EQ(Hex::fromHex("0"), 0);
    ASSERT_EQ(Hex::fromHex("FFFFFFFFFFFFFFFF"), 0xffff'ffff'ffff'ffff);

    std::string s;
    StringSink sink{s};
    sink << Hex(0xffff).str();
    ASSERT_EQ(s, R"(FFFF)");
}

TEST_F(CheapJsonTest, Dec) {
    using Dec = stack_trace::Dec;
    ASSERT_EQ(Dec(0).str(), "0");
    ASSERT_EQ(Dec(0xffff).str(), "65535");
    ASSERT_EQ(Dec(0xfff0).str(), "65520");
    ASSERT_EQ(Dec(0x8000'0000'0000'0000).str(), "9223372036854775808");
}

TEST_F(CheapJsonTest, DocumentObject) {
    std::string s;
    StringSink sink{s};
    stack_trace::CheapJson env{sink};
    auto doc = env.doc();
    ASSERT_EQ(s, "");
    {
        auto obj = doc.appendObj();
        ASSERT_EQ(s, "{");
    }
    ASSERT_EQ(s, "{}");
}

TEST_F(CheapJsonTest, ScalarStringData) {
    std::string s;
    StringSink sink{s};
    stack_trace::CheapJson env{sink};
    auto doc = env.doc();
    doc.append(123);
    ASSERT_EQ(s, R"(123)");
}

TEST_F(CheapJsonTest, ScalarInt) {
    std::string s;
    StringSink sink{s};
    stack_trace::CheapJson env{sink};
    auto doc = env.doc();
    doc.append("hello");
    ASSERT_EQ(s, R"("hello")");
}

TEST_F(CheapJsonTest, ObjectNesting) {
    std::string s;
    StringSink sink{s};
    stack_trace::CheapJson env{sink};
    auto doc = env.doc();
    {
        auto obj = doc.appendObj();
        obj.appendKey("k").append(255);
        {
            auto inner = obj.appendKey("obj").appendObj();
            inner.appendKey("innerKey").append("hi");
        }
    }
    ASSERT_EQ(s, R"({"k":255,"obj":{"innerKey":"hi"}})");
}

TEST_F(CheapJsonTest, Arrays) {
    std::string s;
    StringSink sink{s};
    stack_trace::CheapJson env{sink};
    auto doc = env.doc();
    {
        auto obj = doc.appendObj();
        obj.appendKey("k").append(0xFF);
        { obj.appendKey("empty").appendArr(); }
        {
            auto arr = obj.appendKey("arr").appendArr();
            arr.append(240);
            arr.append(241);
            arr.append(242);
        }
    }
    ASSERT_EQ(s, R"({"k":255,"empty":[],"arr":[240,241,242]})");
}

TEST_F(CheapJsonTest, AppendBSONElement) {
    std::string s;
    StringSink sink{s};
    stack_trace::CheapJson env{sink};
    {
        auto obj = env.doc().appendObj();
        for (auto& e : fromjson(R"({"a":1,"arr":[2,123],"emptyO":{},"emptyA":[]})"))
            obj.append(e);
    }
    ASSERT_EQ(s, R"({"a":1,"arr":[2,123],"emptyO":{},"emptyA":[]})");
}


struct RecursionParam {
    std::ostream& out;
    std::vector<std::function<void(RecursionParam&)>> stack;
};

// Pops a callable and calls it. printStackTrace when we're out of callables.
// Calls itself a few times to synthesize a nice big call stack.
MONGO_COMPILER_NOINLINE void recursionTest(RecursionParam& p) {
    if (p.stack.empty()) {
        // I've come to invoke `stack` elements and test `printStackTrace()`,
        // and I'm all out of `stack` elements.
        printStackTrace(p.out);
        return;
    }
    auto f = std::move(p.stack.back());
    p.stack.pop_back();
    f(p);
}

class LogAdapter {
public:
    std::string toString() const {  // LAME
        std::ostringstream os;
        os << *this;
        return os.str();
    }
    friend std::ostream& operator<<(std::ostream& os, const LogAdapter& x) {
        x.doPrint(os);
        return os;
    }

private:
    virtual void doPrint(std::ostream& os) const = 0;
};

template <typename T>
class LogVec : public LogAdapter {
public:
    explicit LogVec(const T& v, StringData sep = ","_sd) : v(v), sep(sep) {}

private:
    void doPrint(std::ostream& os) const override {
        os << std::hex;
        os << "{";
        StringData s;
        for (auto&& e : v) {
            os << s << e;
            s = sep;
        }
        os << "}";
        os << std::dec;
    }
    const T& v;
    StringData sep = ","_sd;
};

class LogJson : public LogAdapter {
public:
    explicit LogJson(const BSONObj& obj) : obj(obj) {}

private:
    void doPrint(std::ostream& os) const override {
        os << tojson(obj, Strict, /*pretty=*/true);
    }
    const BSONObj& obj;
};

auto tlog() {
    auto r = unittest::log();
    r.setIsTruncatable(false);
    return r;
}

uintptr_t fromHex(const std::string& s) {
    return static_cast<uintptr_t>(std::stoull(s, nullptr, 16));
}

std::string getBaseName(std::string path) {
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos)
        return path;
    return path.substr(lastSlash + 1);
}

struct HumanFrame {
    uintptr_t addr;
    std::string soFileName;
    uintptr_t soFileOffset;
    std::string symbolName;
    uintptr_t symbolOffset;
};

std::vector<HumanFrame> parseTraceBody(const std::string& traceBody) {
    std::vector<HumanFrame> r;
    // Three choices:
    //   just raw:      " ???[0x7F0A71AD4238]"
    //   just soFile:   " libfoo.so(+0xABC408)[0x7F0A71AD4238]"
    //   soFile + symb: " libfoo.so(someSym+0x408)[0x7F0A71AD4238]"
    const std::regex re(R"re( ()re"                              // line pattern open
                        R"re((?:)re"                             // choice open non-capturing
                        R"re((\?\?\?))re"                        // capture just raw case "???"
                        R"re(|)re"                               // next choice
                        R"re(([^(]*)\((.*)\+(0x[0-9A-F]+)\))re"  // so "(" sym offset ")"
                        R"re())re"                               // choice close
                        R"re( \[(0x[0-9A-F]+)\])re"              // raw addr suffix
                        R"re()\n)re");                           // line pattern close, newline
    for (auto i = std::sregex_iterator(traceBody.begin(), traceBody.end(), re);
         i != std::sregex_iterator();
         ++i) {
        if (kSuperVerbose) {
            tlog() << "{";
            for (size_t ei = 1; ei < i->size(); ++ei) {
                tlog() << "  {:2d}: `{}`"_format(ei, (*i)[ei]);
            }
            tlog() << "}";
        }
        size_t patternIdx = 1;
        std::string line = (*i)[patternIdx++];
        std::string rawOnly = (*i)[patternIdx++];  // "???" or empty
        std::string soFile = (*i)[patternIdx++];
        std::string symbol = (*i)[patternIdx++];
        std::string offset = (*i)[patternIdx++];
        std::string addr = (*i)[patternIdx++];
        if (kSuperVerbose) {
            tlog() << "    rawOnly:`{}`, soFile:`{}`, symbol:`{}`, offset: `{}`, addr:`{}`"
                      ""_format(rawOnly, soFile, symbol, offset, addr);
        }
        HumanFrame hf{};
        hf.addr = fromHex(addr);
        if (rawOnly.empty()) {
            // known soFile
            hf.soFileName = soFile;
            if (symbol.empty()) {
                hf.soFileOffset = fromHex(offset);
            } else {
                // known symbol
                hf.symbolName = symbol;
                hf.symbolOffset = fromHex(offset);
            }
        }
        r.push_back(hf);
    }
    return r;
}

// Break down a printStackTrace output for a contrived call tree and sanity-check it.
TEST(StackTrace, PosixFormat) {
    if (kIsWindows) {
        return;
    }

    const std::string trace = [&] {
        std::ostringstream os;
        RecursionParam param{os, {3, recursionTest}};
        recursionTest(param);
        return os.str();
    }();

    if (kSuperVerbose) {
        tlog() << "trace:{" << trace << "}";
    }

    std::smatch m;
    ASSERT_TRUE(
        std::regex_match(trace,
                         m,
                         std::regex(R"re(((?: [0-9A-F]+)+)\n)re"  // raw void* list `addrLine`
                                    R"re(----- BEGIN BACKTRACE -----\n)re"  // header line
                                    R"re((.*)\n)re"                         // huge `jsonLine`
                                    R"re(((?:.*\n)+))re"  // multi-line human-readable `traceBody`
                                    R"re(-----  END BACKTRACE  -----\n)re")))  // footer line
        << "trace: {}"_format(trace);
    std::string addrLine = m[1].str();
    std::string jsonLine = m[2].str();
    std::string traceBody = m[3].str();

    if (kSuperVerbose) {
        tlog() << "addrLine:{" << addrLine << "}";
        tlog() << "jsonLine:{" << jsonLine << "}";
        tlog() << "traceBody:{" << traceBody << "}";
    }

    std::vector<uintptr_t> addrs;
    {
        const std::regex re(R"re( ([0-9A-F]+))re");
        for (auto i = std::sregex_iterator(addrLine.begin(), addrLine.end(), re);
             i != std::sregex_iterator();
             ++i) {
            addrs.push_back(fromHex((*i)[1]));
        }
    }
    if (kSuperVerbose) {
        tlog() << "addrs[] = " << LogVec(addrs);
    }

    BSONObj jsonObj = fromjson(jsonLine);  // throwy
    ASSERT_TRUE(jsonObj.hasField("backtrace"));
    ASSERT_TRUE(jsonObj.hasField("processInfo"));

    if (kSuperVerbose) {
        for (auto& elem : jsonObj["backtrace"].Array()) {
            tlog() << "  btelem=\n" << LogJson(elem.Obj());
        }
        tlog() << "  processInfo=\n" << LogJson(jsonObj["processInfo"].Obj());
    }

    ASSERT_TRUE(jsonObj["processInfo"].Obj().hasField("somap"));
    struct SoMapEntry {
        uintptr_t base;
        std::string path;
    };

    std::map<uintptr_t, SoMapEntry> soMap;
    for (const auto& so : jsonObj["processInfo"]["somap"].Array()) {
        auto soObj = so.Obj();
        SoMapEntry ent{};
        ent.base = fromHex(soObj["b"].String());
        if (soObj.hasField("path")) {
            ent.path = soObj["path"].String();
        }
        soMap[ent.base] = ent;
    }

    std::vector<HumanFrame> humanFrames = parseTraceBody(traceBody);

    {
        // Extract all the humanFrames .addr into a vector and match against the addr line.
        std::vector<uintptr_t> humanAddrs;
        std::transform(humanFrames.begin(),
                       humanFrames.end(),
                       std::back_inserter(humanAddrs),
                       [](auto&& h) { return h.addr; });
        ASSERT_TRUE(addrs == humanAddrs) << LogVec(addrs) << " vs " << LogVec(humanAddrs);
    }

    {
        // Match humanFrames against the "backtrace" json array
        auto btArray = jsonObj["backtrace"].Array();
        for (size_t i = 0; i < humanFrames.size(); ++i) {
            const auto& hf = humanFrames[i];
            const BSONObj& bt = btArray[i].Obj();
            ASSERT_TRUE(bt.hasField("b"));
            ASSERT_TRUE(bt.hasField("o"));
            ASSERT_EQUALS(!hf.symbolName.empty(), bt.hasField("s"));

            if (!hf.soFileName.empty()) {
                uintptr_t btBase = fromHex(bt["b"].String());
                auto soEntryIter = soMap.find(btBase);
                // TODO: (SERVER-43420) fails on RHEL6 when looking up `__libc_start_main`.
                // ASSERT_TRUE(soEntryIter != soMap.end()) << "not in soMap: 0x{:X}"_format(btBase);
                if (soEntryIter == soMap.end())
                    continue;
                std::string soPath = getBaseName(soEntryIter->second.path);
                if (soPath.empty()) {
                    // As a special case, the "main" shared object has an empty soPath.
                } else {
                    ASSERT_EQUALS(hf.soFileName, soPath)
                        << "hf.soFileName:`{}`,soPath:`{}`"_format(hf.soFileName, soPath);
                }
            }
            if (!hf.symbolName.empty()) {
                ASSERT_EQUALS(hf.symbolName, bt["s"].String());
            }
        }
    }
}

TEST(StackTrace, WindowsFormat) {
    if (!kIsWindows) {
        return;
    }

    // TODO: rough: string parts are not escaped and can contain the ' ' delimiter.
    const std::string trace = [&] {
        std::ostringstream os;
        RecursionParam param{os, {3, recursionTest}};
        recursionTest(param);
        return os.str();
    }();
    const std::regex re(R"re(()re"                  // line pattern open
                        R"re(([^\\]?))re"           // moduleName: cannot have backslashes
                        R"re(\s*)re"                // pad
                        R"re(.*)re"                 // sourceAndLine: empty, or "...\dir\file(line)"
                        R"re(\s*)re"                // pad
                        R"re((?:)re"                // symbolAndOffset: choice open non-capturing
                        R"re(\?\?\?)re"             //     raw case: "???"
                        R"re(|)re"                  //   or
                        R"re((.*)\+0x[0-9a-f]*)re"  //     "symbolname+0x" lcHex...
                        R"re())re"                  // symbolAndOffset: choice close
                        R"re()\n)re");              // line pattern close, newline
    auto mark = trace.begin();
    for (auto i = std::sregex_iterator(trace.begin(), trace.end(), re); i != std::sregex_iterator();
         ++i) {
        mark = (*i)[0].second;
    }
    ASSERT_TRUE(mark == trace.end())
        << "cannot match suffix: `" << trace.substr(mark - trace.begin()) << "` "
        << "of trace: `" << trace << "`";
}

TEST(StackTrace, Backtrace) {
    stack_trace::Tracer tracer{};
    void* addrs[10];
    size_t n = tracer.backtrace(addrs, 10);
    unittest::log() << n;
    for (size_t i = 0; i < n; ++i) {
        unittest::log() << "   [" << i << "]" << addrs[i] << "\n";
    }
}

TEST(StackTrace, BacktraceWithMetadata) {
    stack_trace::Tracer tracer{};
    void* addrs[10];
    stack_trace::AddressMetadata meta[10];
    size_t n = tracer.backtraceWithMetadata(addrs, meta, 10);
    unittest::log() << n;
    for (size_t i = 0; i < n; ++i) {
        auto& m = meta[i];
        StringData name;
        if (m.symbol)
            name = m.symbol->name;
        unittest::log() << "   [" << i << "]" << ostr(stack_trace::Hex(m.address).str())
            << ":" << name << "\n";
    }
}

#ifdef HAVE_SIGALTSTACK

class StackTraceSigAltStackTest : public unittest::Test {
public:
    using unittest::Test::Test;

    static void handlerPreamble(int sig) {
        unittest::log() << "tid:" << ostr(stdx::this_thread::get_id()) << ", caught signal " << sig
                        << "!\n";
        char storage;
        unittest::log() << "local var:" << (const void*)&storage << "\n";
    }

    static void tryHandler(void (*handler)(int, siginfo_t*, void*)) {
        constexpr int sig = SIGUSR1;
        constexpr size_t kStackSize = size_t{1} << 20;  // 1 MiB
        auto buf = std::make_unique<std::array<unsigned char, kStackSize>>();
        constexpr unsigned char kSentinel = 0xda;
        std::fill(buf->begin(), buf->end(), kSentinel);
        unittest::log() << "sigaltstack buf: [" << std::hex << buf->size() << std::dec << "] @"
                        << std::hex << uintptr_t(buf->data()) << std::dec << "\n";
        stdx::thread thr([&] {
            unittest::log() << "tid:" << ostr(stdx::this_thread::get_id()) << " running\n";
            {
                stack_t ss;
                ss.ss_sp = buf->data();
                ss.ss_size = buf->size();
                ss.ss_flags = 0;
                if (int r = sigaltstack(&ss, nullptr); r < 0) {
                    perror("sigaltstack");
                }
            }
            {
                struct sigaction sa = {};
                sa.sa_sigaction = handler;
                sa.sa_mask = {};
                sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
                if (int r = sigaction(sig, &sa, nullptr); r < 0) {
                    perror("sigaction");
                }
            }
            raise(sig);
            {
                stack_t ss;
                ss.ss_sp = 0;
                ss.ss_size = 0;
                ss.ss_flags = SS_DISABLE;
                if (int r = sigaltstack(&ss, nullptr); r < 0) {
                    perror("sigaltstack");
                }
            }
        });
        thr.join();
        size_t used = std::distance(
            std::find_if(buf->begin(), buf->end(), [](unsigned char x) { return x != kSentinel; }),
            buf->end());
        unittest::log() << "stack used: " << used << " bytes\n";
    }
};

TEST_F(StackTraceSigAltStackTest, Minimal) {
    tryHandler([](int sig, siginfo_t*, void*) { handlerPreamble(sig); });
}

TEST_F(StackTraceSigAltStackTest, Print) {
    tryHandler([](int sig, siginfo_t*, void*) {
        handlerPreamble(sig);
        printStackTrace();
    });
}

TEST_F(StackTraceSigAltStackTest, Backtrace) {
    tryHandler([](int sig, siginfo_t*, void*) {
        handlerPreamble(sig);
        std::array<void*, stack_trace::kFrameMax> addrs;
        stack_trace::Tracer{}.backtrace(addrs.data(), addrs.size());
    });
}

#endif  // HAVE_SIGALTSTACK

#if MONGO_STACKTRACE_BACKEND == MONGO_STACKTRACE_BACKEND_LIBUNWIND
struct ProcNameRecord {
    stack_trace::AddressMetadata meta;
    std::string soFile;  // these strings provide storage for the StringData in meta
    std::string symbol;
};

auto unwWalk() {
    std::vector<std::unique_ptr<ProcNameRecord>> result;
    unw_context_t unwContext;
    if (int r = unw_getcontext(&unwContext); r < 0) {
        std::cerr << "unw_getcontext: " << unw_strerror(r) << "\n";
        return result;
    }
    unw_cursor_t cursor;
    if (int r = unw_init_local(&cursor, &unwContext); r < 0) {
        std::cerr << "unw_init_local: " << unw_strerror(r) << "\n";
        return result;
    }
    while (true) {
        char buf[1024];
        unw_word_t offset = 0;
        unw_word_t pc;
        if (int r = unw_get_reg(&cursor, UNW_REG_IP, &pc); r < 0) {
            std::cerr << "unw_get_reg: " << unw_strerror(r) << "\n";
            return result;
        }
        if (int r = unw_get_proc_name(&cursor, buf, sizeof(buf), &offset); r < 0) {
            std::cerr << "unw_get_proc_name: " << unw_strerror(r) << "\n";
            return result;
        }

        // The PC we get from unw_get_reg is beyond the call instruction.
        --pc;  // to match 'backtrace'

        auto rec = std::make_unique<ProcNameRecord>();
        rec->meta.address = pc;
        rec->symbol = buf;
        rec->meta.symbol = stack_trace::AddressMetadata::NameBase{rec->symbol, pc - offset};
        mergeDlInfo(rec->meta);
        result.push_back(std::move(rec));

        if (int r = unw_step(&cursor); r < 0) {
            std::cerr << "unw_step: " << unw_strerror(r) << "\n";
            return result;
        } else if (r == 0) {
            break;
        }
    }
    return result;
}

TEST(StackTrace, BacktraceWithMetadataVsWalk) {
    std::array<void*, 30> btAddrs;
    std::array<stack_trace::AddressMetadata, btAddrs.size()> meta;
    size_t btAddrsSize = 0;
    std::vector<std::unique_ptr<ProcNameRecord>> walkRecords;
    stack_trace::Tracer tracer{};
    stacktrace_test_detail::RecursionParam param{
        10, [&] {
            walkRecords = unwWalk();
            btAddrsSize = tracer.backtraceWithMetadata(btAddrs.data(),
                                                       meta.data(),
                                                       btAddrs.size());
        }};
    stacktrace_test_detail::recurseWithLinkage(param);

    unittest::log() << "backtraceWithMetadata results: " << btAddrsSize;

    using stack_trace::Hex;

    auto printMeta = [](const stack_trace::AddressMetadata& meta) {
        std::ostringstream oss;
        stack_trace::OstreamSink sink(oss);
        printOneMetadata(meta, sink);
        return oss.str();
    };

    for (size_t i = 0; i < btAddrsSize; ++i) {
        const auto& m = meta[i];
        unittest::log() << "   [" << i << "] " << printMeta(m);
    }

    unittest::log() << "unw_step results: " << walkRecords.size();
    for (size_t i = 0; i < walkRecords.size(); ++i) {
        const auto& m = walkRecords[i]->meta;
        unittest::log() << "   [" << i << "] " << printMeta(m);
    }
}
#endif  // MONGO_STACKTRACE_BACKEND

/**
 * Try to backtrace from a stack containing a libc function. To do this
 * we need a libc function that makes a user-provided callback, like qsort.
 */
TEST(StackTrace, BacktraceThroughLibc) {
    struct Result {
        void notify() {
            if (called)
                return;
            called = true;
            arrSize = stack_trace::Tracer{}.backtrace(arr.data(), arr.size());
        }

        bool called = 0;
        std::array<void*, stack_trace::kFrameMax> arr;
        size_t arrSize;
    };
    Result capture;
    auto cmp = +[](const void* a, const void* b, void* arg) -> int {
        static_cast<Result*>(arg)->notify();
        return a < b;  // Order them by position in the array.
    };
    std::array<int, 2> arr{{}};
    qsort_r(arr.data(), arr.size(), sizeof(arr[0]), cmp, &capture);
    unittest::log() << "caught [" << capture.arrSize << "]:";
    for (size_t i = 0; i < capture.arrSize; ++i) {
        unittest::log() << "  [" << i << "] " << capture.arr[i];
    }
}

}  // namespace
}  // namespace mongo
