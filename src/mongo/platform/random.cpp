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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kDefault

#include "mongo/platform/basic.h"

#include "mongo/platform/random.h"

#include <string.h>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <errno.h>
#endif

#define _CRT_RAND_S
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>

#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"

#ifdef _WIN32
#define SECURE_RANDOM_BCRYPT
#elif defined(__linux__) || defined(__sun) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__EMSCRIPTEN__)
#define SECURE_RANDOM_URANDOM
#elif defined(__OpenBSD__)
#define SECURE_RANDOM_ARCFOUR
#else
#error Must implement SecureRandom for platform
#endif

namespace mongo {

namespace random_detail {

#if defined(SECURE_RANDOM_BCRYPT)
class SecureUrbg::State {
public:
    State() {
        auto ntstatus = ::BCryptOpenAlgorithmProvider(
            &algHandle, BCRYPT_RNG_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0);
        if (ntstatus != STATUS_SUCCESS) {
            error() << "Failed to open crypto algorithm provider while creating secure random "
                       "object; NTSTATUS: "
                    << ntstatus;
            fassertFailed(28815);
        }
    }

    ~State() {
        auto ntstatus = ::BCryptCloseAlgorithmProvider(algHandle, 0);
        if (ntstatus != STATUS_SUCCESS) {
            warning() << "Failed to close crypto algorithm provider destroying secure random "
                         "object; NTSTATUS: "
                      << ntstatus;
        }
    }

    uint64_t operator()() {
        uint64_t value;
        auto ntstatus =
            ::BCryptGenRandom(algHandle, reinterpret_cast<PUCHAR>(&value), sizeof(value), 0);
        if (ntstatus != STATUS_SUCCESS) {
            error() << "Failed to generate random number from secure random object; NTSTATUS: "
                    << ntstatus;
            fassertFailed(28814);
        }
        return value;
    }

    BCRYPT_ALG_HANDLE algHandle;
};

SecureUrbg::SecureUrbg() : _state(std::make_unique<State>()) {}

uint64_t SecureUrbg::operator()() {
    return (*_state)();
}
#endif  // SECURE_RANDOM_BCRYPT

#if defined(SECURE_RANDOM_URANDOM)
class SecureUrbg::State {
public:
    static constexpr const char* kFn = "/dev/urandom";
    State() {
        in.rdbuf()->pubsetbuf(buf.data(), buf.size());
        in.open(kFn, std::ios::binary | std::ios::in);
        if (!in.is_open()) {
            error() << "cannot open " << kFn << " " << strerror(errno);
            fassertFailed(28839);
        }
    }
    uint64_t operator()() {
        uint64_t r;
        in.read(reinterpret_cast<char*>(&r), sizeof(r));
        if (in.fail()) {
            error() << "InputStreamSecureRandom failed to generate random bytes";
            fassertFailed(28840);
        }
        return r;
    }

    // Reduce buffering. std::ifstream default is 8kiB, quite a lot for "/dev/urandom".
    // SecureRandom objects will likely be asked for only a few words.
    std::array<char, 64> buf;
    std::ifstream in;
};

SecureUrbg::SecureUrbg() : _state{std::make_unique<State>()} {}
uint64_t SecureUrbg::operator()() {
    return (*_state)();
}
#endif  // SECURE_RANDOM_URANDOM

#if defined(SECURE_RANDOM_ARCFOUR)
class SecureUrbg::State {};  // not used
SecureUrbg::SecureUrbg() = default;
uint64_t SecureUrbg::operator()() {
    uint64_t value;
    arc4random_buf(&value, sizeof(value));
    return value;
}
#endif  // SECURE_RANDOM_ARCFOUR

SecureUrbg::~SecureUrbg() = default;

}  // namespace random_detail

std::unique_ptr<SecureRandom> SecureRandom::create() {
    return std::make_unique<SecureRandom>();
}

}  // namespace mongo
