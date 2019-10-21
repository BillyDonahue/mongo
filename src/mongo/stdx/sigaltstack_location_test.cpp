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
#include <stdexcept>

#ifndef _WIN32
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

int runTest() {
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
    std::cout << "sigaltstack test PASS." << std::endl;
    return EXIT_SUCCESS;
}

}  // namespace
}  // namespace mongo::stdx

int main() {
    return mongo::stdx::runTest();
}

#endif  // MONGO_HAS_SIGALTSTACK
