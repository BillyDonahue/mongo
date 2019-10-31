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

#include "mongo/util/stacktrace_threads.h"

#ifdef __linux__

#include <dirent.h>
#include <signal.h>
#include <sys/syscall.h>
#include <thread_db.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <vector>

#include "mongo/util/stacktrace.h"
#include "mongo/util/stacktrace_json.h"
#include "mongo/base/string_data.h"
#include "mongo/stdx/thread.h"

namespace mongo {
namespace stack_trace {

namespace {
namespace all_threads_detail {

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


StringData getBaseName(StringData path) {
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos)
        return path;
    return path.substr(lastSlash + 1);
}

class SimpleSpinLock {
public:
    void lock() {
        while (_flag.test_and_set(std::memory_order_acquire)) ;
    }
    void unlock() {
        _flag.clear(std::memory_order_release);
    }
    auto makeGuard() {
        struct Guard {
            explicit Guard(SimpleSpinLock& csl) : _csl{csl} {
                _csl.lock();
            }
            ~Guard() {
                _csl.unlock();
            }
            SimpleSpinLock& _csl;
        };
        return Guard{*this};
    }

private:
    std::atomic_flag _flag = ATOMIC_FLAG_INIT;
};

template <typename T>
class AsyncStack {
public:
    T* pop_front() {
        auto g = _spin.makeGuard();
        T* node = _head;
        if (node) {
            node = std::exchange(_head, node->intrusiveNext);
            node->intrusiveNext = nullptr;
        }
        return node;
    }

    void push_front(T* node) {
        auto g = _spin.makeGuard();
        node->intrusiveNext = std::exchange(_head, node);
    }

private:
    T* _head = nullptr;
    SimpleSpinLock _spin;  // guards _head
};

class Message {
public:
    Message* intrusiveNext;

    static const size_t capacity = stack_trace::kFrameMax;

    int tid = 0;
    std::unique_ptr<void*[]> addrStorage = std::make_unique<void*[]>(capacity);
    void** addrs = addrStorage.get();
    size_t size = 0;
};

class State {
public:
    static const int kWorkerSignal = SIGURG;
    static const size_t kMessagesCapacity = 1000;

    /** If it's not called, we don't bother to create it. */
    static State& instance() {
        static std::once_flag once;
        std::call_once(once, []{
            _instance = new State;
            installAction();
        });
        return *_instance;
    }

    void printStacks(Sink& sink, bool serially);

private:
    State() {
        for (auto& m : _messageStorage) {
            _free.push_front(&m);
        }
    }

    Message* waitPop(AsyncStack<Message>* stack) {
        while (true) {
            if (Message* m = stack->pop_front())
                return m;
            timespec ts{0, 1'000'000};  // 1msec
            nanosleep(&ts, nullptr);  // nanosleep is AS-Safe.
        }
    }

    void action() {
        int tid = syscall(SYS_gettid);
        Message* msg = waitPop(&_free);
        msg->tid = tid;
        msg->size = stack_trace::Tracer{}.backtrace(msg->addrs, msg->capacity);
        _messages.push_front(msg);
    }

    static void installAction() {
        struct sigaction sa = {};
        sa.sa_sigaction = +[](int, siginfo_t*, void*) { _instance->action(); };
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        if (sigaction(kWorkerSignal, &sa, nullptr) != 0) {
            perror("sigaction");
        }
    }

    void messageToJson(CheapJson::Value& jsonThreads, Sink& sink, const Message& msg);

    Message _messageStorage[kMessagesCapacity];
    AsyncStack<Message> _free;
    AsyncStack<Message> _messages;

    static inline State* _instance = nullptr;
};

void State::printStacks(Sink& sink, bool serially) {
    CheapJson json{sink};
    json.pretty();
    auto jsonDoc = json.doc();
    auto jsonThreads = jsonDoc.appendArr();

    if (serially) {
        iterateTids([&](int tid) {
            if (int r = tgkill(getpid(), tid, kWorkerSignal); r < 0) {
                perror("tgkill");
                return;
            }
            Message* message = waitPop(&_messages);
            messageToJson(jsonThreads, sink, *message);
            _free.push_front(message);
        });
    } else {
        size_t threadCount = 0;
        iterateTids([&](int tid) {
            if (int r = tgkill(getpid(), tid, kWorkerSignal); r < 0) {
                perror("tgkill");
                return;
            }
            ++threadCount;
        });

        while (threadCount) {
            Message* message = waitPop(&_messages);
            --threadCount;
            messageToJson(jsonThreads, sink, *message);
            _free.push_front(message);
        }
    }
}

void State::messageToJson(CheapJson::Value& jsonThreads, Sink& sink, const Message& msg) {
    stack_trace::Tracer tracer{};

    auto jsonThreadObj = jsonThreads.appendObj();
    jsonThreadObj.appendKey("tid").append(msg.tid);
    auto jsonFrames = jsonThreadObj.appendKey("frames").appendArr();

    for (size_t ai = 0; ai < msg.size; ++ai) {
        auto jsonFrame = jsonFrames.appendObj();
        void* const addrPtr = msg.addrs[ai];
        const uintptr_t addr = reinterpret_cast<uintptr_t>(addrPtr);
        jsonFrame.appendKey("a").append(Hex(addr).str());  // insecure: test only
        stack_trace::AddressMetadata meta;
        if (tracer.getAddrInfo(addrPtr, &meta) != 0)
            continue;
        if (meta.soFile) {
            jsonFrame.appendKey("libN").append(getBaseName(meta.soFile->name));
            jsonFrame.appendKey("libB").append(Hex(meta.soFile->base).str());
            jsonFrame.appendKey("libO").append(Hex(addr - meta.soFile->base).str());
        }
        if (meta.symbol) {
            jsonFrame.appendKey("symN").append(meta.symbol->name);
            jsonFrame.appendKey("symO").append(Hex(addr - meta.symbol->base).str());
        }
    }
}

}  // namespace all_threads_detail
}  // namespace

void printAllThreadStacks(Sink& sink, bool serially) {
    all_threads_detail::State::instance().printStacks(sink, serially);
}

}  // namespace stack_trace
}  // namespace mongo

#endif  // __linux__
