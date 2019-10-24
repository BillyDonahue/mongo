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

#include "mongo/stdx/thread.h"

#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <setjmp.h>
#include <stdexcept>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !MONGO_HAS_SIGALTSTACK

int main() {
    std::cout << "`sigaltstack` testing skipped on this platform." << std::endl;
    return EXIT_SUCCESS;
}

#else  // MONGO_HAS_SIGALTSTACK

namespace mongo::stdx {
namespace {

int stackLocationTest() {
    struct ChildThreadInfo {
        stack_t ss;
        const char* handlerLocal;
    };
    static ChildThreadInfo childInfo{};
    static const int kSignal = SIGUSR1;

    auto childFunction = [&] {
        // Use sigaltstack's `old_ss` parameter to query the installed sigaltstack.
        if (sigaltstack(nullptr, &childInfo.ss)) {
            perror("sigaltstack");
            abort();
        }

        // Make sure kSignal is unblocked.
        sigset_t sigset;
        sigemptyset(&sigset);
        sigaddset(&sigset, kSignal);
        if (sigprocmask(SIG_UNBLOCK, &sigset, nullptr)) {
            perror("sigprocmask");
            exit(EXIT_FAILURE);
        }

        // Install sigaction for kSignal. Be careful to specify SA_ONSTACK.
        struct sigaction sa;
        sa.sa_sigaction = +[](int, siginfo_t*, void*) {
            char n;
            childInfo.handlerLocal = &n;
        };
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        if (sigaction(kSignal, &sa, nullptr)) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }
        // `raise` waits for signal handler to complete.
        // https://pubs.opengroup.org/onlinepubs/009695399/functions/raise.html
        raise(kSignal);
    };

    stdx::thread childThread{childFunction};
    childThread.join();

    if (childInfo.ss.ss_flags & SS_DISABLE) {
        std::cerr << "Child thread unexpectedly had sigaltstack disabled.\n";
        exit(EXIT_FAILURE);
    }

    uintptr_t altStackBegin = reinterpret_cast<uintptr_t>(childInfo.ss.ss_sp);
    uintptr_t altStackEnd = altStackBegin + childInfo.ss.ss_size;
    uintptr_t handlerLocal = reinterpret_cast<uintptr_t>(childInfo.handlerLocal);

    std::cerr << std::hex << "child sigaltstack = [" << altStackBegin << ", " << altStackEnd
              << ")\n"
              << "handlerLocal = " << handlerLocal << "\n"
              << "             = sigaltstack + " << (handlerLocal - altStackBegin) << "\n"
              << std::dec;

    if (handlerLocal < altStackBegin || handlerLocal >= altStackEnd) {
        std::cerr << "Handler local address " << handlerLocal << " was outside of: ["
                  << altStackBegin << ", " << altStackEnd << ")\n"
                  << std::dec;
        exit(EXIT_FAILURE);
    }
    return EXIT_SUCCESS;
}

void infiniteRecursion(size_t& i, void** deepest) {
    if (++i == 0) return;  // Avoid the UB of truly infinite recursion.
    *deepest = &deepest;
    infiniteRecursion(i, deepest);
}

template <bool kSigAltStack>
int recursionTestImpl() {
    static const int kSignal = SIGSEGV;

    // Make sure kSignal is unblocked.
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, kSignal);
    if (sigprocmask(SIG_UNBLOCK, &sigset, nullptr)) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }

    static std::atomic<int> seen = 0;
    static sigjmp_buf sigjmp;

    // Install sigaction for kSignal. Be careful to specify SA_ONSTACK.
    struct sigaction sa;
    sa.sa_sigaction = +[](int, siginfo_t*, void*) {
        ++seen;
        siglongjmp(sigjmp, 1);  // goto the recovery path.
    };
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(kSignal, &sa, nullptr)) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    auto childFunction = [&] {
        // Disable sigaltstack to see what happens. Process should die.
        if (!kSigAltStack) {
            stack_t ss{};
            ss.ss_flags = SS_DISABLE;
            if (sigaltstack(&ss, nullptr)) {
                perror("disable sigaltstack");
                abort();
            }
            std::cout << "disabled the sigaltstack\n";
        }

        size_t depth = 0;
        void* deepAddr = &deepAddr;

        // There are special rules about where `setjmp` can appear.
        if (sigsetjmp(sigjmp, 1)) {
            // Fake return from signal handler's `siglongjmp`.
            ptrdiff_t stackSpan = (intptr_t)&deepAddr - (intptr_t)deepAddr;
            std::cout << "Recovered from SIGSEGV after stack depth=" << depth
                      << ", stack spans approximately " << (stackSpan / 1024) << " kiB.\n";
            std::cout << "That is " << (1. * stackSpan / depth) << " bytes per frame\n";
            return;
        }
        infiniteRecursion(depth, &deepAddr);
    };

    stdx::thread childThread{childFunction};
    while (seen != 1) {
        struct timespec req {
            0, 1'000
        };  // 1 usec
        nanosleep(&req, nullptr);
    }
    childThread.join();
    return EXIT_SUCCESS;
}

int recursionTest() {
    return recursionTestImpl<true>();
}

int recursionDeathTest() {
    if (pid_t kidPid = fork(); kidPid == 0) {
        recursionTestImpl<false>();  // child process
        std::cout << "Child process failed to crash!\n";
        return EXIT_SUCCESS;  // Shouldn't make it this far.
    } else {
        int wstatus;
        while (true) {
            pid_t waited = waitpid(kidPid, &wstatus, 0);
            if (waited == kidPid)
                break;
        }
        if (WIFEXITED(wstatus)) {
            std::cout << "child exited with: " << WEXITSTATUS(wstatus) << "\n";
            return EXIT_FAILURE;
        }
        if (WIFSIGNALED(wstatus)) {
            int kidSignal = WTERMSIG(wstatus);
            std::cout << "child died of signal: " << WTERMSIG(kidSignal) << "\n";
            if (kidSignal == SIGSEGV) {
                return EXIT_SUCCESS;
            }
        }
        return EXIT_FAILURE;
    }
}

}  // namespace
}  // namespace mongo::stdx

int main() {
    struct Test {
        const char* name;
        int (*func)();
    } static constexpr kTests[] = {
        {"stackLocationTest", mongo::stdx::stackLocationTest},
        {"recursionTest", mongo::stdx::recursionTest},
        {"recursionDeathTest", mongo::stdx::recursionDeathTest},
    };
    for (auto& test : kTests) {
        std::cout << "\n===== " << test.name << " begin:\n";
        if (int r = test.func(); r != EXIT_SUCCESS) {
            std::cout << test.name << " FAIL\n";
            return r;
        }
        std::cout << "===== " << test.name << " PASS\n";
    }
    return EXIT_SUCCESS;
}

#endif  // MONGO_HAS_SIGALTSTACK
