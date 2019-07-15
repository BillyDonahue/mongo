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

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>
#include <fmt/format.h>

#include "mongo/base/initializer.h"
#include "mongo/base/status.h"
#include "mongo/logger/logger.h"
#include "mongo/unittest/benchmark_options_gen.h"
#include "mongo/unittest/unittest.h"
#include "mongo/unittest/unittest_options_gen.h"
#include "mongo/util/options_parser/environment.h"
#include "mongo/util/options_parser/option_section.h"
#include "mongo/util/options_parser/options_parser.h"
#include "mongo/util/signal_handlers_synchronous.h"

namespace mongo {
namespace unittest {
namespace {

namespace moe = optionenvironment;

Status configureBenchmark(char* progname, int verbosity, const moe::Environment& environment) {
    static const char* const benchmarkFlags[] = {"benchmark_filter",
                                                 "benchmark_min_time",
                                                 "benchmark_repetitions",
                                                 "benchmark_report_aggregates_only",
                                                 "benchmark_format",
                                                 "benchmark_out_format",
                                                 "benchmark_out",
                                                 "benchmark_color",
                                                 "benchmark_counters_tabular"};
    auto formatFlag = [](const char* name, auto&& value) {
        using namespace fmt::literals;
        return "--{}={}"_format(name, value);
    };
    std::vector<std::string> bmArgs;
    for (const char* f : benchmarkFlags) {
        moe::Value v;
        invariant(environment.get(f, &v));
        bmArgs.push_back(formatFlag(f, v.toString()));
    }
    if (verbosity > 0) {
        bmArgs.push_back(formatFlag("v", verbosity));
    }

    std::vector<char*> bmArgv;
    // Benchmark library retains the progname forever, so it should be the global argv[0].
    bmArgv.push_back(progname);
    for (auto& a : bmArgs) {
        bmArgv.push_back(a.data());
    }
    int bmArgc = bmArgv.size();
    benchmark::Initialize(&bmArgc, bmArgv.data());
    if (benchmark::ReportUnrecognizedArguments(bmArgc, bmArgv.data())) {
        return Status(ErrorCodes::BadValue, "invalid flags passed to benchmark library");
    }
    return Status::OK();
}

}  // namespace
}  // namespace unittest
}  // namespace mongo

using mongo::Status;

namespace moe = ::mongo::optionenvironment;

int main(int argc, char** argv, char** envp) {
    ::mongo::clearSignalMask();
    ::mongo::setupSynchronousSignalHandlers();

    ::mongo::runGlobalInitializersOrDie(argc, argv, envp);

    moe::OptionSection options;

    if (Status status = mongo::unittest::addUnitTestOptions(&options); !status.isOK()) {
        std::cerr << status;
        return EXIT_FAILURE;
    }

    if (Status status = mongo::unittest::addBenchmarkOptions(&options); !status.isOK()) {
        std::cerr << status;
        return EXIT_FAILURE;
    }

    moe::OptionsParser parser;
    moe::Environment environment;
    std::map<std::string, std::string> env;
    std::vector<std::string> argVector(argv, argv + argc);
    Status ret = parser.run(options, argVector, env, &environment);
    if (!ret.isOK()) {
        std::cerr << options.helpString();
        return EXIT_FAILURE;
    }

    bool list = false;
    moe::StringVector_t suites;
    std::string filter;
    int repeat = 1;
    std::string verbose;
    bool listBenchmarks = false;
    bool runBenchmarks = false;
    // These will be assigned with default values, if not present.
    invariant(environment.get("list", &list));
    invariant(environment.get("repeat", &repeat));
    invariant(environment.get("listBenchmarks", &listBenchmarks));
    invariant(environment.get("runBenchmarks", &runBenchmarks));
    // The default values of these are empty.
    environment.get("suite", &suites).ignore();
    environment.get("filter", &filter).ignore();
    environment.get("verbose", &verbose).ignore();

    if (verbose.find_first_not_of("v") != verbose.npos) {
        std::cerr << "The --verbose option cannot contain characters other than 'v'\n"
                  << options.helpString();
        return EXIT_FAILURE;
    }
    int verbosity = verbose.size();

    ::mongo::logger::globalLogDomain()->setMinimumLoggedSeverity(
        ::mongo::logger::LogSeverity::Debug(verbosity));

    if (list) {
        auto suiteNames = ::mongo::unittest::getAllSuiteNames();
        for (auto name : suiteNames) {
            std::cout << name << std::endl;
        }
        return EXIT_SUCCESS;
    }

    if (listBenchmarks) {
        char arg1[] = "--benchmark_list_tests";
        std::array<char*, 2> bmArgv{{argv[0], arg1}};
        int bmArgc = bmArgv.size();
        benchmark::Initialize(&bmArgc, bmArgv.data());
        benchmark::RunSpecifiedBenchmarks();  // Really just lists benchmarks
        return EXIT_SUCCESS;
    }

    if (auto status = mongo::unittest::configureBenchmark(argv[0], verbosity, environment);
        !status.isOK()) {
        std::cerr << status;
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;

    if (runBenchmarks) {
        std::cout << "Running test program benchmarks\n";
#ifndef MONGO_CONFIG_OPTIMIZED_BUILD
        log() << "***WARNING*** MongoDB was built with --opt=off. Function timings "
                 "may be affected. Always verify any code change against the production "
                 "environment (e.g. --opt=on).";
#endif
        benchmark::RunSpecifiedBenchmarks();
    } else {
        result = ::mongo::unittest::Suite::run(suites, filter, repeat);
    }

    ret = ::mongo::runGlobalDeinitializers();
    if (!ret.isOK()) {
        std::cerr << "Global deinitilization failed: " << ret.reason() << std::endl;
    }

    return result;
}
