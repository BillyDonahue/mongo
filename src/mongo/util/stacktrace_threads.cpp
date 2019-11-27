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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kCommand

#include "mongo/util/stacktrace.h"

#ifdef __linux__

#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fmt/format.h>
#include <mutex>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/stdx/thread.h"
#include "mongo/util/log.h"
#include "mongo/util/signal_handlers_synchronous.h"
#include "mongo/util/stacktrace_json.h"
#include "mongo/util/stacktrace_somap.h"

namespace mongo {
namespace stack_trace_detail {

namespace {

constexpr StringData kTaskDir = "/proc/self/task"_sd;

class CachedMetaGenerator {
public:
    CachedMetaGenerator() : _cache{2000} {}
    ~CachedMetaGenerator() {
        log() << "CachedMetaGenerator: " << _hits << "/" << (_hits + _misses);
    }
    const StackTraceAddressMetadata& load(void* addr) {
        auto it = _cache.find(addr);
        if (it == _cache.end()) {
            ++_misses;
            it = _cache.insert({addr, _gen.load(addr)}).first;
        } else {
            ++_hits;
        }
        return it->second;
    }

private:
    size_t _hits = 0;
    size_t _misses = 0;
    std::unordered_map<void*, StackTraceAddressMetadata> _cache;
    StackTraceAddressMetadataGenerator _gen;
};

void sleepMicros(int64_t usec) {
    auto nsec = usec * 1000;
    constexpr static int64_t k1E9 = 1'000'000'000;
    timespec ts{nsec / k1E9, nsec % k1E9};
    nanosleep(&ts, nullptr);
}

int gettid() {
    return syscall(SYS_gettid);
}


int tgkill(int pid, int tid, int sig) {
    return syscall(SYS_tgkill, pid, tid, sig);
}

template <typename F>
void iterateTids(F&& f) {
    int selfTid = gettid();
    DIR* taskDir = opendir("/proc/self/task");
    if (!taskDir) {
        perror("opendir");
        return;
    }
    while (true) {
        errno = 0;
        struct dirent* dent = readdir(taskDir);
        if (!dent) {
            if (errno)
                perror("readdir");
            break;  // null result with unchanged errno means end of dir.
        }
        char* endp = nullptr;
        int tid = static_cast<int>(strtol(dent->d_name, &endp, 10));
        if (*endp)
            continue;  // Ignore non-integer names (e.g. "." or "..").
        if (tid == selfTid)
            continue;  // skip self
        f(tid);
    }
    if (closedir(taskDir))
        perror("closedir");
}

bool tidExists(int tid) {
    char filename[kTaskDir.size() + 32];  // kTaskDir plus enough space for a tid
    auto it = format_to(std::begin(filename), FMT_STRING("{}/{}"), kTaskDir, tid);
    *it++ = '\0';
    struct stat statbuf;
    return stat(filename, &statbuf) == 0;
}

std::string readThreadName(int tid) {
    static constexpr auto kComm = "comm"_sd;
    std::string name;
    std::string filename = format(FMT_STRING("{}/{}/{}"), kTaskDir, tid, kComm);
    int fd = open(filename.data(), O_RDONLY);
    if (fd == -1) {
        perror("open");
        return name;
    }
    name.resize(128);  // Really only 16 on linux but we don't need to care.

    // "Fast file" /proc won't give us a short read or a EINTR.
    if (ssize_t rr = read(fd, name.data(), name.size()); rr == -1) {
        name.clear();
        close(fd);
        return name;
    } else {
        name.resize(rr);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
    }
    if (close(fd) == -1) {
        perror("close");
        return name;
    }
    return name;
}

StringData getBaseName(StringData path) {
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos)
        return path;
    return path.substr(lastSlash + 1);
}

/** Cannot yield. AS-Safe. */
class SimpleSpinLock {
public:
    void lock() {
        while (true) {
            for (int i = 0; i < 100; ++i) {
                if (!_flag.test_and_set(std::memory_order_acquire)) {
                    return;
                }
            }
            sleepMicros(1);
        }
    }
    void unlock() {
        _flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag _flag = ATOMIC_FLAG_INIT;
};

template <typename T>
class AsyncStack {
public:
    T* tryPop() {
        std::lock_guard lock{_spin};
        T* node = _head;
        if (node) {
            node = std::exchange(_head, node->intrusiveNext);
            node->intrusiveNext = nullptr;
        }
        return node;
    }

    void push(T* node) {
        std::lock_guard lock{_spin};
        node->intrusiveNext = std::exchange(_head, node);
    }

private:
    T* _head = nullptr;
    SimpleSpinLock _spin;  // guards _head
};

class Message {
public:
    Message* intrusiveNext;

    static const size_t capacity = kStackTraceFrameMax;

    auto addrRange() const {
        struct AddrRange {
            void **b, **e;
            void** begin() const {
                return b;
            }
            void** end() const {
                return e;
            }
        };
        return AddrRange{addrs, addrs + size};
    }

    int tid = 0;
    std::unique_ptr<void*[]> addrStorage = std::make_unique<void*[]>(capacity);
    void** addrs = addrStorage.get();
    size_t size = 0;
};

class State {
public:
    /** if `redactAddr`, suppress any raw address fields, consistent with ASLR. */
    void printStacks(StackTraceSink& sink, bool redactAddrs = true);

    /**
     * We need signals for two purpposes in the stack tracing system.
     *
     * An external process sends a signal to initiate stack tracing.  When that's received,
     * we *also* need a signal to send to each thread to cause to dump its backtrace.
     * The `siginfo` provides enough information to allow one signal to serve both roles.
     *
     * Since all threads are open to receiving this signal, any of them can be selected to
     * receive it when it comes from outside. So we arrange for any thread that receives the
     * undirected stack trace signal to re-issue it directy at the signal processing thread.
     *
     * The signal processing thread will have the signal blocked, and handle it
     * synchronously with sigwaitinfo, so this handler only applies to the other
     * respondents.
     */
    void action(siginfo_t* si);

    void markProcessingThread() {
        _processingTid.store(gettid(), std::memory_order_release);
    }

    void setSignal(int signal) {
        _signal = signal;
    }

private:
    /** An in-flight all-thread stack collection. */
    struct CollectionOperation {
        AsyncStack<Message> pool;
        AsyncStack<Message> results;
    };

    void messageToJson(CheapJson::Value& jsonThreads,
                       const Message& msg,
                       bool redact,
                       CachedMetaGenerator* metaGen);

    void printAllThreadStacksFormat(StackTraceSink& sink,
                                    std::vector<Message*> received,
                                    bool redactAddrs);

    Message* acquireMessageBuffer() {
        while (true) {
            if (Message* msg = _collectionOperation.load()->pool.tryPop(); msg)
                return msg;
            sleepMicros(1);
        }
    }

    void postMessage(Message* msg) {
        _collectionOperation.load()->results.push(msg);
    }

    int _signal = 0;
    std::atomic<int> _processingTid = -1;
    std::atomic<CollectionOperation*> _collectionOperation = nullptr;
};

void State::printAllThreadStacksFormat(StackTraceSink& sink,
                                       std::vector<Message*> received,
                                       bool redactAddrs) {
    CheapJson jsonEnv{sink};
    auto jsonDoc = jsonEnv.doc();
    jsonDoc.setPretty(true);
    auto jsonRootObj = jsonDoc.appendObj();
    {
        CachedMetaGenerator metaGen;
        auto jsonThreadInfoKey = jsonRootObj.appendKey("threadInfo");
        auto jsonThreadInfoArr = jsonThreadInfoKey.appendArr();
        for (Message* message : received) {
            messageToJson(jsonThreadInfoArr, *message, redactAddrs, &metaGen);
        }
    }
    {
        auto jsonProcInfoKey = jsonRootObj.appendKey("processInfo");
        auto jsonProcInfo = jsonProcInfoKey.appendObj();
        const BSONObj& bsonProcInfo = globalSharedObjectMapInfo().obj();
        for (const BSONElement& be : bsonProcInfo) {
            StringData key = be.fieldNameStringData();
            if (be.type() == BSONType::Array && key == "somap"_sd) {
                // special case handling for the `somap` array.
                auto jsonSoMapKey = jsonProcInfo.appendKey(key);
                auto jsonSoMap = jsonSoMapKey.appendArr();
                for (const BSONElement& ae : be.Array()) {
                    BSONObj bRec = ae.embeddedObject();
                    auto jsonSoMapElemObj = jsonSoMap.appendObj();
                    jsonSoMapElemObj.setPretty(false);
                    // {
                    //  "b":"7F1815A87000",
                    //  "path":"libkrb5.so.3",
                    //  "elfType":3,
                    //  "buildId":"69FBCF425EE6DF03DE93B82FBC2FC33790E68A96"
                    // },
                    for (auto&& bRecElement : bRec) {
                        jsonSoMapElemObj.append(bRecElement);
                    }
                    // uintptr_t soBase = Hex::fromHex(bRec.getStringField("b"));
                    // jsonSoMapElemObj.append(ae);
                }
                continue;
            }
            jsonProcInfo.append(be);
        }
    }
}

void State::printStacks(StackTraceSink& sink, bool redactAddrs) {
    std::set<int> pendingTids;
    iterateTids([&](int tid) { pendingTids.insert(tid); });
    log() << "gathered " << pendingTids.size() << " pending threads";

    // Make a CollectionOperation and load it with enough Message buffers to serve
    // every pending thread.
    CollectionOperation collection;
    std::vector<Message> messageStorage(pendingTids.size());
    for (auto& m : messageStorage)
        collection.pool.push(&m);
    _collectionOperation.store(&collection, std::memory_order_release);

    log() << "signalling all pending tids";
    for (auto iter = pendingTids.begin(); iter != pendingTids.end();) {
        if (int r = tgkill(getpid(), *iter, _signal); r < 0) {
            int savedErrno = errno;
            log() << "tgkill(" << *iter << "):" << strerror(savedErrno);
            iter = pendingTids.erase(iter);
        } else {
            ++iter;
        }
    }
    log() << "parallel: signalled " << pendingTids.size() << " threads";

    std::vector<Message*> received;
    received.reserve(pendingTids.size());

    size_t napMicros = 0;
    while (!pendingTids.empty()) {
        if (Message* message = collection.results.tryPop(); message) {
            napMicros = 0;
            pendingTids.erase(message->tid);
            received.push_back(message);
        } else if (napMicros < 50'000) {
            // Results queue is dry and we haven't napped enough to justify a reap.
            napMicros += 1'000;
            sleepMicros(1'000);
        } else {
            napMicros = 0;
            // Prune dead threads from the pendingTids set before retrying.
            for (auto iter = pendingTids.begin(); iter != pendingTids.end();) {
                if (!tidExists(*iter)) {
                    log() << "pending tid " << *iter << " is dead";
                    iter = pendingTids.erase(iter);
                } else {
                    ++iter;
                }
            }
        }
    }

    // This operation is completed. Make it unavailable, to identify stragglers.
    _collectionOperation.store(nullptr, std::memory_order_release);

    printAllThreadStacksFormat(sink, received, redactAddrs);

}

void State::messageToJson(CheapJson::Value& jsonThreads,
                          const Message& msg,
                          bool redact,
                          CachedMetaGenerator* metaGen) {
    auto jsonThreadObj = jsonThreads.appendObj();
    if (auto threadName = readThreadName(msg.tid); !threadName.empty())
        jsonThreadObj.appendKey("name").append(threadName);
    jsonThreadObj.appendKey("tid").append(msg.tid);
    auto jsonFrames = jsonThreadObj.appendKey("backtrace").appendArr();

    for (void* const addrPtr : msg.addrRange()) {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(addrPtr);
        auto jsonFrame = jsonFrames.appendObj();
        jsonFrame.setPretty(false);  // Compactly print one frame object per line.
        if (!redact)
            jsonFrame.appendKey("a").append(Hex(addr));
        const auto& meta = metaGen->load(addrPtr);
        if (meta.file()) {
            // TODO: remap the 'base' to an identifier that will appear in the
            // processInfo.somap of the greater stacktrace json object.
            jsonFrame.appendKey("b").append(getBaseName(meta.file().name()));
            if (!redact)
                jsonFrame.appendKey("bAddr").append(Hex(meta.file().base()));
            jsonFrame.appendKey("o").append(Hex(addr - meta.file().base()));
        }
        if (meta.symbol()) {
            jsonFrame.appendKey("s").append(meta.symbol().name());
            jsonFrame.appendKey("sOffset").append(Hex(addr - meta.symbol().base()));
        }
    }
}

void State::action(siginfo_t* si) {
    int sigTid = _processingTid.load(std::memory_order_acquire);
    switch (si->si_code) {
        case SI_USER:
        case SI_QUEUE:
            // Received from outside. Forward to signal processing thread if there is one.
            if (sigTid != -1)
                tgkill(getpid(), sigTid, si->si_signo);
            break;
        case SI_TKILL: {
            // Received from the signal processing thread.
            // Submit this thread's backtrace to the results stack.
            Message* msg = acquireMessageBuffer();
            msg->tid = gettid();
            msg->size = rawBacktrace(msg->addrs, msg->capacity);
            postMessage(msg);
        } break;
    }
}

std::once_flag instanceFlag;
State* instancePtr = nullptr;

State& instance() {
    // Avoiding local static init for AS-Safety.
    std::call_once(instanceFlag, [] { instancePtr = new State; });
    return *instancePtr;
}

void installHandler(int signal) {
    instance().setSignal(signal);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = +[](int, siginfo_t* si, void*) { instance().action(si); };
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(signal, &sa, nullptr) != 0) {
        int savedErr = errno;
        severe() << format(FMT_STRING("Failed to install sigaction for signal {} ({})"),
                           signal,
                           strerror(savedErr));
        fassertFailed(31376);
    }
}


}  // namespace
}  // namespace stack_trace_detail

void printAllThreadStacks(StackTraceSink& sink) {
    stack_trace_detail::instance().printStacks(sink);
}

void setupStackTraceSignalAction(int signal) {
    stack_trace_detail::installHandler(signal);
}

void markAsStackTraceProcessingThread() {
    stack_trace_detail::instance().markProcessingThread();
}

}  // namespace mongo

#endif  // __linux__
