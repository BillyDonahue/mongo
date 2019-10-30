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

#include <array>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <signal.h>
#include <vector>
#include "mongo/util/stacktrace.h"
#include "mongo/base/string_data.h"
#include "mongo/stdx/thread.h"

namespace mongo {
namespace stack_trace {

namespace {

/** State for the forEachThread call */
struct ForEachThreadState {
    void** buffer;
    size_t bufferCapacity;
    size_t bufferSize;
};

std::vector<ThreadInfo> allThreads() {
    std::vector<ThreadInfo> res;
    DIR* taskDir = opendir("/proc/self/task");
    if (!taskDir) {
        perror("opendir");
        return res;
    }

    while (true) {
        errno = 0;
        auto dirent = readdir(taskDir);
        if (!dirent) {
            if (errno) {
                perror("readdir");
            }
            break;
        }
        StringData name = dirent->d_name;
        char* endp = nullptr;
        int tid = static_cast<int>(strtol(dirent->d_name, &endp, 10));
        if (*endp) {
            // std::cerr << "skip non-tid filename " << dirent->d_name << "\n";
            continue;
        }
        res.push_back(ThreadInfo{tid});
    }
    if (closedir(taskDir))
        perror("closedir");

    return res;
}

}  // namespace

#ifdef __linux__
void forEachThread(void(*f)(const ThreadInfo*, void*), void* arg) {
    std::vector<ThreadInfo> threads = allThreads();
    for (const ThreadInfo& thread : threads) {
        f(&thread, arg);
    }
}
#endif // __linux__

}  // namespace stack_trace
}  // namespace mongo
