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
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>
#include <fmt/printf.h>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <regex>
#include <signal.h>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/json.h"
#include "mongo/config.h"
#include "mongo/stdx/thread.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/stacktrace.h"
#include "mongo/util/stacktrace_json.h"

/** `sigaltstack` was introduced in glibc-2.12 in 2010. */
#if !defined(_WIN32)
#define HAVE_SIGALTSTACK
#endif

#ifdef __linux__
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif  //  __linux__

namespace mongo {

namespace stack_trace_test_detail {
/** Needs to have linkage so we can test metadata. */
void testFunctionWithLinkage() {
    printf("...");
}

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

}  // namespace stack_trace_test_detail

namespace {

using namespace fmt::literals;

constexpr bool kSuperVerbose = 0;  // Devel instrumentation

#if defined(_WIN32)
constexpr bool kIsWindows = true;
#else
constexpr bool kIsWindows = false;
#endif

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
    std::string trace;
    stack_trace_test_detail::RecursionParam param{3, [&] {
                                                      StringStackTraceSink sink{trace};
                                                      printStackTrace(sink);
                                                  }};
    stack_trace_test_detail::recurseWithLinkage(param, 3);

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
        std::string s;
        stack_trace_test_detail::RecursionParam param{3, [&] {
                                                          StringStackTraceSink sink{s};
                                                          printStackTrace(sink);
                                                      }};
        stack_trace_test_detail::recurseWithLinkage(param);
        return s;
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

std::string traceString() {
    std::ostringstream os;
    printStackTrace(os);
    return os.str();
}

/** Emit a stack trace before main() runs, for use in the EarlyTraceSanity test. */
std::string earlyTrace = traceString();

/**
 * Verify that the JSON object emitted as part of a stack trace contains a "processInfo"
 * field, and that it, in turn, contains the fields expected by mongosymb.py. Verify in
 * particular that a stack trace emitted before main() and before the MONGO_INITIALIZERS
 * have run will be valid according to mongosymb.py.
 */
TEST(StackTrace, EarlyTraceSanity) {
    if (kIsWindows) {
        return;
    }

    const std::string trace = traceString();
    const std::string substrings[] = {
        R"("processInfo":)",
        R"("gitVersion":)",
        R"("compiledModules":)",
        R"("uname":)",
        R"("somap":)",
    };
    for (const auto& sub : substrings) {
        ASSERT_STRING_CONTAINS(earlyTrace, sub);
    }
    for (const auto& sub : substrings) {
        ASSERT_STRING_CONTAINS(trace, sub);
    }
}

#ifndef _WIN32
// `MetadataGenerator::load` should fill its meta member with something reasonable.
// Only testing with functions which we expect the dynamic linker to know about, so
// they must have external linkage.
TEST(StackTrace, MetadataGenerator) {
    StackTraceAddressMetadataGenerator gen;
    struct {
        void* ptr;
        std::string fileSub;
        std::string symbolSub;
    } const tests[] = {
        {
            reinterpret_cast<void*>(&stack_trace_test_detail::testFunctionWithLinkage),
            "stacktrace_test",
            "testFunctionWithLinkage",
        },
        {
            // printf's file tricky (surprises under ASAN, Mac, ...),
            // but we should at least get a symbol name containing "printf" out of it.
            reinterpret_cast<void*>(&std::printf),
            {},
            "printf",
        },
    };

    for (const auto& test : tests) {
        const auto& meta = gen.load(test.ptr);
        if (kSuperVerbose) {
            OstreamStackTraceSink sink{std::cout};
            meta.printTo(sink);
        }
        ASSERT_EQUALS(meta.address(), reinterpret_cast<uintptr_t>(test.ptr));
        if (!test.fileSub.empty()) {
            ASSERT_TRUE(meta.file());
            ASSERT_STRING_CONTAINS(meta.file().name(), test.fileSub);
        }

        if (!test.symbolSub.empty()) {
            ASSERT_TRUE(meta.symbol());
            ASSERT_STRING_CONTAINS(meta.symbol().name(), test.symbolSub);
        }
    }
}

TEST(StackTrace, MetadataGeneratorFunctionMeasure) {
    // Measure the size of a C++ function as a test of metadata retrieval.
    // Load increasing addresses until the metadata's symbol name changes.
    StackTraceAddressMetadataGenerator gen;
    void* fp = reinterpret_cast<void*>(&stack_trace_test_detail::testFunctionWithLinkage);
    const auto& meta = gen.load(fp);
    if (!meta.symbol())
        return;  // No symbol for `fp`. forget it.
    std::string savedSymbol{meta.symbol().name()};
    uintptr_t fBase = meta.symbol().base();
    ASSERT_EQ(fBase, reinterpret_cast<uintptr_t>(fp))
        << "function pointer should match its symbol base";
    size_t fSize = 0;
    for (; true; ++fSize) {
        auto& m = gen.load(reinterpret_cast<void*>(fBase + fSize));
        if (!m.symbol() || m.symbol().name() != savedSymbol)
            break;
    }
    // Place some reasonable expectation on the size of the tiny test function.
    ASSERT_GT(fSize, 0);
    ASSERT_LT(fSize, 512);
}
#endif  // _WIN32

#ifdef HAVE_SIGALTSTACK
class StackTraceSigAltStackTest : public unittest::Test {
public:
    using unittest::Test::Test;

    template <typename T>
    static std::string ostr(const T& v) {
        std::ostringstream os;
        os << v;
        return os.str();
    }

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
        std::array<void*, kStackTraceFrameMax> addrs;
        rawBacktrace(addrs.data(), addrs.size());
    });
}
#endif  // HAVE_SIGALTSTACK

class CheapJsonTest : public unittest::Test {
public:
    using unittest::Test::Test;
    using CheapJson = stack_trace_detail::CheapJson;
    using Hex = stack_trace_detail::Hex;
    using Dec = stack_trace_detail::Dec;
};

TEST_F(CheapJsonTest, Appender) {
    std::string s;
    StringStackTraceSink sink{s};
    sink << "Hello"
         << ":" << Dec(0) << ":" << Hex(255) << ":" << Dec(1234567890);
    ASSERT_EQ(s, "Hello:0:FF:1234567890");
}

TEST_F(CheapJsonTest, Hex) {
    ASSERT_EQ(StringData(Hex(static_cast<void*>(0))), "0");
    ASSERT_EQ(StringData(Hex(0xffff)), "FFFF");
    ASSERT_EQ(Hex(0xfff0), "FFF0");
    ASSERT_EQ(Hex(0x8000'0000'0000'0000), "8000000000000000");
    ASSERT_EQ(Hex::fromHex("FFFF"), 0xffff);
    ASSERT_EQ(Hex::fromHex("0"), 0);
    ASSERT_EQ(Hex::fromHex("FFFFFFFFFFFFFFFF"), 0xffff'ffff'ffff'ffff);

    std::string s;
    StringStackTraceSink sink{s};
    sink << Hex(0xffff);
    ASSERT_EQ(s, R"(FFFF)");
}

TEST_F(CheapJsonTest, DocumentObject) {
    std::string s;
    StringStackTraceSink sink{s};
    CheapJson env{sink};
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
    StringStackTraceSink sink{s};
    CheapJson env{sink};
    auto doc = env.doc();
    doc.append(123);
    ASSERT_EQ(s, R"(123)");
}

TEST_F(CheapJsonTest, ScalarInt) {
    std::string s;
    StringStackTraceSink sink{s};
    CheapJson env{sink};
    auto doc = env.doc();
    doc.append("hello");
    ASSERT_EQ(s, R"("hello")");
}

TEST_F(CheapJsonTest, ObjectNesting) {
    std::string s;
    StringStackTraceSink sink{s};
    CheapJson env{sink};
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
    StringStackTraceSink sink{s};
    CheapJson env{sink};
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
    StringStackTraceSink sink{s};
    CheapJson env{sink};
    {
        auto obj = env.doc().appendObj();
        for (auto& e : fromjson(R"({"a":1,"arr":[2,123],"emptyO":{},"emptyA":[]})"))
            obj.append(e);
    }
    ASSERT_EQ(s, R"({"a":1,"arr":[2,123],"emptyO":{},"emptyA":[]})");
}

TEST_F(CheapJsonTest, Pretty) {
    std::string s;
    StringStackTraceSink sink{s};
    CheapJson env{sink};
    auto doc = env.doc();
    doc.setPretty(true);
    {
        auto obj = doc.appendObj();
        obj.appendKey("headerKey").append(255);
        {
            auto inner = obj.appendKey("inner").appendObj();
            inner.appendKey("innerKey").append("hi");
        }
        obj.appendKey("footerKey").append(123);
    }

    ASSERT_EQ(s, R"({
  "headerKey":255,
  "inner":{
    "innerKey":"hi"},
  "footerKey":123})"_sd);
}

#ifdef __linux__

class LogstreamBuilderSink : public StackTraceSink {
public:
    LogstreamBuilderSink(logger::LogstreamBuilder& v) : _log(v) {}

    void doWrite(StringData s) override {
        _log << s;
    }

private:
    logger::LogstreamBuilder& _log;
};

class NullSink : public StackTraceSink {
public:
    void doWrite(StringData) override {}
};

#if defined(MONGO_USE_LIBUNWIND)
class PrintAllThreadStacksTest : public unittest::Test {
public:
    void doPrintAllThreadStacks(size_t numThreads) {
        // NullSink sink;
        auto tlogStream = tlog();
        auto tlogSink = LogstreamBuilderSink{tlogStream};
        StackTraceSink& sink = tlogSink;

        std::vector<stdx::thread> workers;

        std::mutex mutex;
        std::condition_variable cv;
        bool endAll = false;

        for (size_t i = 0; i < numThreads; ++i) {
            workers.push_back(stdx::thread([&] {
                std::unique_lock lock{mutex};
                while (true) {
                    using namespace std::literals::chrono_literals;
                    if (cv.wait_for(lock, 10ms, [&] { return endAll; }))
                        return;
                }
            }));
        }
        printAllThreadStacks(sink);
        {
            std::unique_lock lock{mutex};
            endAll = true;
        }
        cv.notify_all();
        for (auto& thr : workers) {
            thr.join();
        }
    }
};

TEST_F(PrintAllThreadStacksTest, Go_2_Threads) {
    doPrintAllThreadStacks(2);
}
TEST_F(PrintAllThreadStacksTest, Go_20_Threads) {
    doPrintAllThreadStacks(20);
}
TEST_F(PrintAllThreadStacksTest, Go_200_Threads) {
    doPrintAllThreadStacks(200);
}
TEST_F(PrintAllThreadStacksTest, Go_1000_Threads) {
    doPrintAllThreadStacks(1000);
}
TEST_F(PrintAllThreadStacksTest, Go_2000_Threads) {
    doPrintAllThreadStacks(2000);
}
TEST_F(PrintAllThreadStacksTest, Go_5000_Threads) {
    doPrintAllThreadStacks(5000);
}
TEST_F(PrintAllThreadStacksTest, Go_6000_Threads) {
    doPrintAllThreadStacks(6000);
}
// 6000 threads takes 3sec, but 7000 threads takes 250sec!
// TEST_F(PrintAllThreadStacksTest, Go_7000_Threads) { doPrintAllThreadStacks(7000); }

#endif  // defined(MONGO_USE_LIBUNWIND)

#endif  // __linux__

#if defined(MONGO_USE_LIBUNWIND) || defined(MONGO_CONFIG_HAVE_EXECINFO_BACKTRACE)
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
            arrSize = rawBacktrace(arr.data(), arr.size());
        }
        bool called = 0;
        std::array<void*, kStackTraceFrameMax> arr;
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
#endif  // mongo stacktrace backend


}  // namespace
}  // namespace mongo
