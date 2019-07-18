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

#include <iostream>
#include <string>
#include <vector>

#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include "mongo/base/initializer.h"
#include "mongo/base/status.h"
#include "mongo/logger/logger.h"
#include "mongo/unittest/unittest.h"
#include "mongo/unittest/unittest_options_gen.h"
#include "mongo/util/options_parser/environment.h"
#include "mongo/util/options_parser/option_section.h"
#include "mongo/util/options_parser/options_parser.h"
#include "mongo/util/signal_handlers_synchronous.h"

using mongo::Status;

namespace moe = ::mongo::optionenvironment;

namespace mongo {
namespace unittest {
namespace {

static const char* const kMongoCatchReporterName = "mongoUnitTest";

class MongoCatchReporter : public Catch::StreamingReporterBase<MongoCatchReporter> {
    using Base = Catch::StreamingReporterBase<MongoCatchReporter>;
public:
    MongoCatchReporter(const Catch::ReporterConfig& config) : Base(config) { }
    ~MongoCatchReporter() override = default;

    static std::string getDescription() {
        return "Catch TEST_CASE as a mongo unittest Suite";
    }

    void testCaseStarting(const Catch::TestCaseInfo& testInfo ) override {
        ++indent;
        std::cerr << indentation()
            << "testCaseStarting: `" << testInfo.name << "`\n";
    }

    void sectionStarting(const Catch::SectionInfo& sectionInfo) override {
        ++indent;
        std::cerr << indentation()
            << "sectionStarting: `" << sectionInfo.name << "`\n";
    }

    void sectionEnded(const Catch::SectionStats& sectionStats ) override {
        std::cerr << indentation()
            << "sectionEnded: `" << sectionStats.sectionInfo.name << "`\n";
        --indent;
    }

    void testCaseEnded(const Catch::TestCaseStats& testCaseStats ) override {
        std::cerr << indentation()
            << "testCaseEnded: `" << testCaseStats.testInfo.name << "`\n";
        --indent;
    }

    void assertionStarting(const Catch::AssertionInfo& assertionInfo) override {}
    bool assertionEnded(const Catch::AssertionStats& assertionStats) override { return false; }

    std::string indentation() const { return std::string(4*indent, ' '); }

    int indent = 0;
};

CATCH_REGISTER_REPORTER(kMongoCatchReporterName, MongoCatchReporter);

} // namespace
} // namespace unittest
} // namespace mongo


int main(int argc, char** argv, char** envp) {
    ::mongo::clearSignalMask();
    ::mongo::setupSynchronousSignalHandlers();

    ::mongo::runGlobalInitializersOrDie(argc, argv, envp);

    moe::OptionSection options;

    Status status = mongo::unittest::addUnitTestOptions(&options);
    if (!status.isOK()) {
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
    // "list" and "repeat" will be assigned with default values, if not present.
    invariant(environment.get("list", &list));
    invariant(environment.get("repeat", &repeat));
    // The default values of "suite" "filter" and "verbose" are empty.
    environment.get("suite", &suites).ignore();
    environment.get("filter", &filter).ignore();
    environment.get("verbose", &verbose).ignore();

    if (std::any_of(verbose.cbegin(), verbose.cend(), [](char ch) { return ch != 'v'; })) {
        std::cerr << "The string for the --verbose option cannot contain characters other than 'v'"
                  << std::endl;
        std::cerr << options.helpString();
        return EXIT_FAILURE;
    }
    ::mongo::logger::globalLogDomain()->setMinimumLoggedSeverity(
        ::mongo::logger::LogSeverity::Debug(verbose.length()));

    std::shared_ptr<Catch::IReporterFactory> mongoReporterFactory;

    Catch::Session catchSession;

    {
        // Configure Catch2.
        //Catch::ConfigData& configData = catchSession.configData();

        catchSession.configData().reporterName = mongo::unittest::kMongoCatchReporterName;

        //(void)configData;

        std::cerr << "getAllTestCasesSorted:\n";
        for (const auto& tc : getAllTestCasesSorted(catchSession.config())) {
            std::cerr << "  - `" << tc.name << "`, tags:";
            for (const auto& tag : tc.tags) {
                std::cerr << "`" << tag << "`, ";
            }
            std::cerr << "\n";
        }

        std::cerr << "getTestsOrTags:\n";
        for (auto&& t : catchSession.config().getTestsOrTags()) {
            std::cerr << "  - " << t << "\n";
        }
        std::cerr << "getSectionsToRun:\n";
        for (auto&& t : catchSession.config().getSectionsToRun()) {
            std::cerr << "  - " << t << "\n";
        }
    }

    if (list) {
        auto suiteNames = ::mongo::unittest::getAllSuiteNames();
        for (auto name : suiteNames) {
            std::cout << name << std::endl;
        }
        return EXIT_SUCCESS;
    }

    int result = EXIT_SUCCESS;

    {
        std::cerr << "Catch2 Begin: result=" << result << "\n";
        result = (result || catchSession.run());
        std::cerr << "Catch2 End: result=" << result << "\n";
    }

    {
        std::cerr << "unittest::Suite Begin: result=" << result << "\n";
        result = (result || ::mongo::unittest::Suite::run(suites, filter, repeat));
        std::cerr << "unittest::Suite End: result=" << result << "\n";
    }

    ret = ::mongo::runGlobalDeinitializers();
    if (!ret.isOK()) {
        std::cerr << "Global deinitilization failed: " << ret.reason() << std::endl;
    }

    return result;
}
