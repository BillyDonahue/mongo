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

#include "mongo/util/signal_handlers_synchronous.h"

#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/exception.hpp>
#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <streambuf>
#include <typeinfo>

#include "mongo/base/string_data.h"
#include "mongo/logger/log_domain.h"
#include "mongo/logger/logger.h"
#include "mongo/platform/compiler.h"
#include "mongo/stdx/thread.h"
#include "mongo/util/concurrency/thread_name.h"
#include "mongo/util/debug_util.h"
#include "mongo/util/debugger.h"
#include "mongo/util/exception_filter_win32.h"
#include "mongo/util/exit_code.h"
#include "mongo/util/log.h"
#include "mongo/util/quick_exit.h"
#include "mongo/util/stacktrace.h"
#include "mongo/util/text.h"

namespace mongo {

namespace {

#if defined(_WIN32)
const char* strsignal(int signalNum) {
    // should only see SIGABRT on windows
    switch (signalNum) {
        case SIGABRT:
            return "SIGABRT";
        default:
            return "UNKNOWN";
    }
}

void endProcessWithSignal(int signalNum) {
    RaiseException(EXIT_ABRUPT, EXCEPTION_NONCONTINUABLE, 0, nullptr);
}

#else  // ! defined(_WIN32)

void endProcessWithSignal(int signalNum) {
    // This works by restoring the system-default handler for the given signal and re-raising it, in
    // order to get the system default termination behavior (i.e., dumping core, or just exiting).
    struct sigaction defaultedSignals;
    memset(&defaultedSignals, 0, sizeof(defaultedSignals));
    defaultedSignals.sa_handler = SIG_DFL;
    sigemptyset(&defaultedSignals.sa_mask);
    invariant(sigaction(signalNum, &defaultedSignals, nullptr) == 0);
    raise(signalNum);
}

#endif  // ! defined(_WIN32)

class MallocFreeOStream : public std::ostream {
public:
    MallocFreeOStream() : std::ostream(&_buf) {}
    MallocFreeOStream(const MallocFreeOStream&) = delete;
    MallocFreeOStream& operator=(const MallocFreeOStream&) = delete;

    StringData str() const {
        return _buf.str();
    }
    void rewind() {
        _buf.rewind();
    }

private:
    class Streambuf : public std::streambuf {
    public:
        Streambuf() {
            setp(_buffer, _buffer + maxLogLineSize);
        }
        Streambuf(const Streambuf&) = delete;
        Streambuf& operator=(const Streambuf&) = delete;

        StringData str() const {
            return StringData(pbase(), pptr() - pbase());
        }
        void rewind() {
            setp(pbase(), epptr());
        }

    private:
        static const size_t maxLogLineSize = 100 * 1000;
        char _buffer[maxLogLineSize];
    };

    Streambuf _buf;
};

/**
 * Instances of this type guard the MallocFreeOStream. While locking a mutex isn't guaranteed to
 * be signal-safe, this file does it anyway. The assumption is that the main safety risk to
 * locking a mutex is that you could deadlock with yourself. That risk is protected against by
 * only locking the mutex in fatal functions that log then exit. There is a remaining risk that
 * one of these functions recurses (possible if logging segfaults while handing a
 * segfault). This is currently acceptable because if things are that broken, there is little we
 * can do about it.
 *
 * If in the future, we decide to be more strict about posix signal safety, we could switch to
 * an atomic test-and-set loop, possibly with a mechanism for detecting signals raised while
 * handling other signals.
 */
class MallocFreeOStreamGuard {
public:
    explicit MallocFreeOStreamGuard() : _lk(_streamMutex, stdx::defer_lock) {
        if (_terminateDepth++) {
            quickExit(EXIT_ABRUPT);
        }
        _lk.lock();
    }

    MallocFreeOStream& mfos() { return _mfos; }

private:
    inline static MallocFreeOStream _mfos;
    inline static stdx::mutex _streamMutex;
    inline static thread_local int _terminateDepth = 0;
    stdx::unique_lock<stdx::mutex> _lk;
};

// must hold MallocFreeOStreamGuard to call
void writeMallocFreeStreamToLog(MallocFreeOStream& mfos) {
    logger::globalLogDomain()
        ->append(logger::MessageEventEphemeral(Date_t::now(),
                                               logger::LogSeverity::Severe(),
                                               getThreadName(),
                                               mfos.str())
                     .setIsTruncatable(false))
        .transitional_ignore();
    mfos.rewind();
}

// must hold MallocFreeOStreamGuard to call
void printSignalAndBacktrace(int signalNum, MallocFreeOStream& mfos) {
    mfos << "Got signal: " << signalNum << " (" << strsignal(signalNum) << ").\n";
#if defined(_WIN32)
    printStackTraceFromSignal(mfos);
#else  // !defined(_WIN32)
    {
        TempFileStackTraceSink sink;
        printStackTraceFromSignal(sink);
        sink.rewind();
        while (sink.good()) {
            char buf[256];
            StringData data = sink.read(buf, sizeof(buf));
            mfos << data;
        }
    }
#endif  // !defined(_WIN32)
    writeMallocFreeStreamToLog(mfos);
}

// this will be called in certain c++ error cases, for example if there are two active
// exceptions
void myTerminate() {
    MallocFreeOStreamGuard lk{};

    // In c++11 we can recover the current exception to print it.
    if (std::current_exception()) {
        lk.mfos() << "terminate() called. An exception is active;"
                          << " attempting to gather more information";
        writeMallocFreeStreamToLog(lk.mfos());

        const std::type_info* typeInfo = nullptr;
        try {
            try {
                throw;
            } catch (const DBException& ex) {
                typeInfo = &typeid(ex);
                lk.mfos() << "DBException::toString(): " << redact(ex) << '\n';
            } catch (const std::exception& ex) {
                typeInfo = &typeid(ex);
                lk.mfos() << "std::exception::what(): " << redact(ex.what()) << '\n';
            } catch (const boost::exception& ex) {
                typeInfo = &typeid(ex);
                lk.mfos() << "boost::diagnostic_information(): "
                                  << boost::diagnostic_information(ex) << '\n';
            } catch (...) {
                lk.mfos() << "A non-standard exception type was thrown\n";
            }

            if (typeInfo) {
                const std::string name = demangleName(*typeInfo);
                lk.mfos() << "Actual exception type: " << name << '\n';
            }
        } catch (...) {
            lk.mfos() << "Exception while trying to print current exception.\n";
            if (typeInfo) {
                // It is possible that we failed during demangling. At least try to print the
                // mangled name.
                lk.mfos() << "Actual exception type: " << typeInfo->name() << '\n';
            }
        }
    } else {
        lk.mfos() << "terminate() called. No exception is active";
    }

    printStackTrace(lk.mfos());
    writeMallocFreeStreamToLog(lk.mfos());
    breakpoint();
    endProcessWithSignal(SIGABRT);
}

void abruptQuit(int signalNum) {
    MallocFreeOStreamGuard lk{};
    printSignalAndBacktrace(signalNum, lk.mfos());
    breakpoint();
    endProcessWithSignal(signalNum);
}

#if defined(_WIN32)

void myInvalidParameterHandler(const wchar_t* expression,
                               const wchar_t* function,
                               const wchar_t* file,
                               unsigned int line,
                               uintptr_t pReserved) {
    severe() << "Invalid parameter detected in function " << toUtf8String(function)
             << " File: " << toUtf8String(file) << " Line: " << line;
    severe() << "Expression: " << toUtf8String(expression);
    severe() << "immediate exit due to invalid parameter";

    abruptQuit(SIGABRT);
}

void myPureCallHandler() {
    severe() << "Pure call handler invoked";
    severe() << "immediate exit due to invalid pure call";
    abruptQuit(SIGABRT);
}

#else  // !defined(_WIN32)

void abruptQuitWithAddrSignal(int signalNum, siginfo_t* siginfo, void* ucontext_erased) {
    // For convenient debugger access.
    MONGO_COMPILER_VARIABLE_UNUSED auto ucontext = static_cast<const ucontext_t*>(ucontext_erased);

    MallocFreeOStreamGuard lk{};

    const char* action = (signalNum == SIGSEGV || signalNum == SIGBUS) ? "access" : "operation";
    lk.mfos() << "Invalid " << action << " at address: " << siginfo->si_addr;

    // Writing out message to log separate from the stack trace so at least that much gets
    // logged. This is important because we may get here by jumping to an invalid address which
    // could cause unwinding the stack to break.
    writeMallocFreeStreamToLog(lk.mfos());

    printSignalAndBacktrace(signalNum, lk.mfos());
    breakpoint();
    endProcessWithSignal(signalNum);
}

#endif  // !defined(_WIN32)

}  // namespace

void setupSynchronousSignalHandlers() {
    std::set_terminate(myTerminate);
    std::set_new_handler(reportOutOfMemoryErrorAndExit);

#if defined(_WIN32)
    invariant(signal(SIGABRT, abruptQuit) != SIG_ERR);
    _set_purecall_handler(myPureCallHandler);
    _set_invalid_parameter_handler(myInvalidParameterHandler);
    setWindowsUnhandledExceptionFilter();
#else  // !defined(_WIN32)

    {
        enum SignalKind { kIgnore, kAbruptQuit, kAbruptQuitWithAddr };
        struct {
            int n;
            SignalKind style;
        } const specs[] = {
            { SIGHUP, kIgnore},
            { SIGUSR2, kIgnore},
            { SIGPIPE, kIgnore},

            // ^\ is the stronger ^C. Log and quit hard without waiting for cleanup.
            {SIGQUIT, kAbruptQuit},
            {SIGABRT, kAbruptQuit},

            {SIGSEGV, kAbruptQuitWithAddr},
            {SIGBUS, kAbruptQuitWithAddr},
            {SIGILL, kAbruptQuitWithAddr},
            {SIGFPE, kAbruptQuitWithAddr},
        };
        for (const auto& spec : specs) {
            struct sigaction action;
            memset(&action, 0, sizeof(action));
            sigemptyset(&action.sa_mask);
            switch (spec.style) {
                case kIgnore:
                    action.sa_handler = SIG_IGN;
                    break;
                case kAbruptQuit:
                    action.sa_handler = &abruptQuit;
                    break;
                case kAbruptQuitWithAddr:
                    action.sa_sigaction = &abruptQuitWithAddrSignal;
                    action.sa_flags = SA_SIGINFO;
                    break;
            }
            invariant(sigaction(spec.n, &action, nullptr) == 0);
        }
    }
    setupSIGTRAPforGDB();
#endif  // !defined(_WIN32)
}

void reportOutOfMemoryErrorAndExit() {
    MallocFreeOStreamGuard lk{};
    printStackTrace(lk.mfos() << "out of memory.\n");
    writeMallocFreeStreamToLog(lk.mfos());
    quickExit(EXIT_ABRUPT);
}

void clearSignalMask() {
#ifndef _WIN32
    // We need to make sure that all signals are unmasked so signals are handled correctly
    sigset_t unblockSignalMask;
    invariant(sigemptyset(&unblockSignalMask) == 0);
    invariant(sigprocmask(SIG_SETMASK, &unblockSignalMask, nullptr) == 0);
#endif
}
}  // namespace mongo
