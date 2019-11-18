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

#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fmt/format.h>
#include <mutex>
#include <sys/syscall.h>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/stdx/thread.h"
#include "mongo/util/log.h"
#include "mongo/util/signal_handlers_synchronous.h"
#include "mongo/util/stacktrace_json.h"

namespace mongo {
namespace stack_trace_detail {

namespace {

constexpr const char kTaskDir[] = "/proc/self/task";

void sleepMicros(int64_t usec) {
    auto nsec = usec * 1000;
    constexpr static int64_t k1E9 = 1'000'000'000;
    timespec ts{nsec / k1E9, nsec % k1E9};
    nanosleep(&ts, nullptr);
}

int tgkill(int pid, int tid, int sig) {
    return syscall(SYS_tgkill, pid, tid, sig);
}

template <typename F>
void iterateTids(F&& f) {
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
        f(tid);
    }
    if (closedir(taskDir))
        perror("closedir");
}

bool tidExists(int tid) {
    char filename[sizeof(kTaskDir) + 32];  // kTaskDir plus enough space for a tid
    auto it = format_to(std::begin(filename), FMT_STRING("{}/{}"), StringData(kTaskDir), tid);
    *it++ = '\0';
    struct stat statbuf;
    return (stat(filename, &statbuf) == 0);
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
    T* pop_front() {
        std::lock_guard lock{_spin};
        T* node = _head;
        if (node) {
            node = std::exchange(_head, node->intrusiveNext);
            node->intrusiveNext = nullptr;
        }
        return node;
    }

    void push_front(T* node) {
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

    int tid = 0;
    std::unique_ptr<void*[]> addrStorage = std::make_unique<void*[]>(capacity);
    void** addrs = addrStorage.get();
    size_t size = 0;
};

class State {
public:
    static const size_t kMessagesCapacity = 100;

    static int getTid() {
        return syscall(SYS_gettid);
    }

    /** If it's not called, we don't bother to create it. */
    static State& instance() {
        static std::once_flag once;
        std::call_once(once, [] { _instance = new State; });
        return *_instance;
    }

    void printStacks(StackTraceSink& sink, int signal, bool serially, bool redactAddr);

    void action() {
        // Signal was sent from the signal processing thread.
        // Submit the caller's backtrace to the global message collector.
        Message* msg = waitPop(&_free);
        msg->tid = getTid();
        msg->size = rawBacktrace(msg->addrs, msg->capacity);
        _messages.push_front(msg);
    }

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
    static void stackTraceSignalAction(int sig, siginfo_t* siginfo, void*) {
        switch (siginfo->si_code) {
            case SI_USER:
            case SI_QUEUE:
                // Received from outside. Pass it to the signal processing thread if there is one.
                if (int spTid = instance().processingTid(); spTid != -1)
                    syscall(SYS_tgkill, getpid(), spTid, sig);
                break;
            case SI_TKILL: {
                instance().action();
            } break;
        }
    }

    void setup(int signal) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sigemptyset(&sa.sa_mask);
        sa.sa_sigaction = &stackTraceSignalAction;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        if (sigaction(signal, &sa, nullptr) != 0) {
            int savedErr = errno;
            severe() << format(
                FMT_STRING("Failed to install signal handler for signal {} with sigaction: {}"),
                signal,
                strerror(savedErr));
            fassertFailed(31376);
        }
    }

    void markProcessingThread() {
        _processingTid.store(getTid(), std::memory_order_release);
    }

private:
    State() {
        for (auto& m : _messageStorage) {
            _free.push_front(&m);
        }
    }

    void printStacksSerially(CheapJson::Value& jsonThreads, int signal, bool redactAddr);
    void printStacksParallel(CheapJson::Value& jsonThreads, int signal, bool redactAddr);

    Message* waitPop(AsyncStack<Message>* stack) {
        while (true) {
            if (Message* m = stack->pop_front())
                return m;
            sleepMicros(1'000);
        }
    }

    // If `redactAddr`, then we don't emit `addr` and `fileaddr` fields, to respect address
    // space randomization.
    void messageToJson(CheapJson::Value& jsonThreads, const Message& msg, bool redactAddr);

    int processingTid() {
        return _processingTid.load(std::memory_order_acquire);
    }

    std::atomic<int> _processingTid = -1;
    Message _messageStorage[kMessagesCapacity];
    AsyncStack<Message> _free;
    AsyncStack<Message> _messages;

    static inline State* _instance = nullptr;
};

void State::printStacksSerially(CheapJson::Value& jsonThreads, int signal, bool redactAddr) {
    auto signalOneThread = [&](int tid) {
        if (tid == getTid())
            return;  // skip self
        log() << "signalling tid " << tid;
        if (int r = tgkill(getpid(), tid, signal); r < 0) {
            int savedErrno = errno;
            log() << "tgkill(" << tid << "):" << strerror(savedErrno);
            return;
        }
        while (true) {
            if (Message* message = _messages.pop_front()) {
                messageToJson(jsonThreads, *message, redactAddr);
                _free.push_front(message);
                break;
            } else if (!tidExists(tid)) {
                log() << "tid " << tid << " died";
                break;
            } else {
                sleepMicros(1'000);
            }
        }
    };
    iterateTids(signalOneThread);
}

void State::printStacksParallel(CheapJson::Value& jsonThreads, int signal, bool redactAddr) {
    log() << "parallel: signalling all tids";
    std::set<int> pendingTids;
    iterateTids([&](int tid) {
        if (tid == getTid())
            return;  // skip self
        if (int r = tgkill(getpid(), tid, signal); r < 0) {
            int savedErrno = errno;
            log() << "tgkill(" << tid << "):" << strerror(savedErrno);
            return;
        }
        pendingTids.insert(tid);
    });
    log() << "parallel: signalled all " << pendingTids.size() << " tids";
    while (!pendingTids.empty()) {
        {
            bool served = false;
            while (Message* message = _messages.pop_front()) {
                pendingTids.erase(message->tid);
                messageToJson(jsonThreads, *message, redactAddr);
                _free.push_front(message);
                served = true;
            }
            if (served)
                continue;
        }
        {
            bool served = false;
            for (auto pit = pendingTids.begin(); pit != pendingTids.end();) {
                if (!tidExists(*pit)) {
                    log() << "tid " << *pit << " died";
                    pit = pendingTids.erase(pit);
                    served = true;
                } else {
                    ++pit;
                }
            }
            if (served)
                continue;
        }
        log() << "parallel: sleeping with " << pendingTids.size() << " tids pending";
        sleepMicros(1'000);
    }
}

void State::printStacks(StackTraceSink& sink, int signal, bool serially, bool redactAddr) {
    CheapJson jsonEnv{sink};
    auto jsonDoc = jsonEnv.doc();
    jsonDoc.setPretty(true);
    auto jsonThreadsArray = jsonDoc.appendArr();
    if (serially) {
        printStacksSerially(jsonThreadsArray, signal, redactAddr);
    } else {
        printStacksParallel(jsonThreadsArray, signal, redactAddr);
    }
}

void State::messageToJson(CheapJson::Value& jsonThreads, const Message& msg, bool redactAddr) {
    StackTraceAddressMetadataGenerator metaGen;

    auto jsonThreadObj = jsonThreads.appendObj();
    jsonThreadObj.appendKey("tid").append(msg.tid);
    auto jsonFrames = jsonThreadObj.appendKey("frames").appendArr();

    for (size_t ai = 0; ai < msg.size; ++ai) {
        auto jsonFrame = jsonFrames.appendObj();
        jsonFrame.setPretty(false);
        void* const addrPtr = msg.addrs[ai];
        const uintptr_t addr = reinterpret_cast<uintptr_t>(addrPtr);
        if (!redactAddr)
            jsonFrame.appendKey("addr").append(Hex(addr));  // insecure: test only
        const auto& meta = metaGen.load(addrPtr);
        if (meta.file()) {
            jsonFrame.appendKey("file").append(getBaseName(meta.file().name()));
            if (!redactAddr)
                jsonFrame.appendKey("fileaddr").append(Hex(meta.file().base()));
            jsonFrame.appendKey("fileoff").append(Hex(addr - meta.file().base()));
        }
        if (meta.symbol()) {
            jsonFrame.appendKey("sym").append(meta.symbol().name());
            jsonFrame.appendKey("symoff").append(Hex(addr - meta.symbol().base()));
        }
    }
}

}  // namespace
}  // namespace stack_trace_detail

void printAllThreadStacks(StackTraceSink& sink, int signal, bool serially, bool redactAddr) {
    stack_trace_detail::State::instance().printStacks(sink, signal, serially, redactAddr);
}

void setupStackTraceSignalAction(int signal) {
    stack_trace_detail::State::instance().setup(signal);
}

void markAsStackTraceProcessingThread() {
    stack_trace_detail::State::instance().markProcessingThread();
}

}  // namespace mongo

#endif  // __linux__
