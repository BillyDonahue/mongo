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

/*
 * A C++ unit testing framework.
 *
 * For examples of basic usage, see mongo/unittest/unittest_test.cpp.
 */

#pragma once

#include <boost/preprocessor/cat.hpp>
#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "mongo/base/status_with.h"
#include "mongo/base/string_data.h"
#include "mongo/logger/logstream_builder.h"
#include "mongo/logger/message_log_domain.h"
#include "mongo/unittest/bson_test_util.h"
#include "mongo/unittest/unittest_helpers.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/str.h"

/**
 * Fail unconditionally, reporting the given message.
 */
#define FAIL(MESSAGE) ::mongo::unittest::TestAssertionFailure(__FILE__, __LINE__, MESSAGE).stream()

/**
 * Fails unless "EXPRESSION" is true.
 */
#define ASSERT_TRUE(EXPRESSION) \
    if (!(EXPRESSION))          \
    FAIL("Expected: " #EXPRESSION)
#define ASSERT(EXPRESSION) ASSERT_TRUE(EXPRESSION)

/**
 * Fails if "EXPRESSION" is true.
 */
#define ASSERT_FALSE(EXPRESSION) ASSERT(!(EXPRESSION))

/**
 * Asserts that a Status code is OK.
 */
#define ASSERT_OK(EXPRESSION) ASSERT_EQUALS(::mongo::Status::OK(), (EXPRESSION))

/**
 * Asserts that a status code is anything but OK.
 */
#define ASSERT_NOT_OK(EXPRESSION) ASSERT_NOT_EQUALS(::mongo::Status::OK(), (EXPRESSION))

/*
 * Binary comparison assertions.
 */
#define ASSERT_EQUALS(a, b) ASSERT_EQ(a, b)
#define ASSERT_NOT_EQUALS(a, b) ASSERT_NE(a, b)
#define ASSERT_LESS_THAN(a, b) ASSERT_LT(a, b)
#define ASSERT_NOT_LESS_THAN(a, b) ASSERT_GTE(a, b)
#define ASSERT_GREATER_THAN(a, b) ASSERT_GT(a, b)
#define ASSERT_NOT_GREATER_THAN(a, b) ASSERT_LTE(a, b)
#define ASSERT_LESS_THAN_OR_EQUALS(a, b) ASSERT_LTE(a, b)
#define ASSERT_GREATER_THAN_OR_EQUALS(a, b) ASSERT_GTE(a, b)

#define ASSERT_EQ(a, b) ASSERT_COMPARISON_(kEq, a, b)
#define ASSERT_NE(a, b) ASSERT_COMPARISON_(kNe, a, b)
#define ASSERT_LT(a, b) ASSERT_COMPARISON_(kLt, a, b)
#define ASSERT_LTE(a, b) ASSERT_COMPARISON_(kLe, a, b)
#define ASSERT_GT(a, b) ASSERT_COMPARISON_(kGt, a, b)
#define ASSERT_GTE(a, b) ASSERT_COMPARISON_(kGe, a, b)

/**
 * Binary comparison utility macro. Do not use directly.
 */
#define ASSERT_COMPARISON_(OP, a, b)                                                           \
    if (auto ca =                                                                              \
            ::mongo::unittest::ComparisonAssertion<::mongo::unittest::ComparisonOp::OP>::make( \
                __FILE__, __LINE__, #a, #b, a, b))                                             \
    ca.failure().stream()

/**
 * Approximate equality assertion. Useful for comparisons on limited precision floating point
 * values.
 */
#define ASSERT_APPROX_EQUAL(a, b, ABSOLUTE_ERR) ASSERT_LTE(std::abs((a) - (b)), ABSOLUTE_ERR)

/**
 * Assert a function call returns its input unchanged.
 */
#define ASSERT_IDENTITY(INPUT, FUNCTION)                                                        \
    [&](auto&& v) {                                                                             \
        if (auto ca = ::mongo::unittest::ComparisonAssertion<                                   \
                ::mongo::unittest::ComparisonOp::kEq>::make(__FILE__,                           \
                                                            __LINE__,                           \
                                                            #INPUT,                             \
                                                            #FUNCTION "(" #INPUT ")",           \
                                                            v,                                  \
                                                            FUNCTION(                           \
                                                                std::forward<decltype(v)>(v)))) \
            ca.failure().stream();                                                              \
    }(INPUT)

/**
 * Verify that the evaluation of "EXPRESSION" throws an exception of type EXCEPTION_TYPE.
 *
 * If "EXPRESSION" throws no exception, or one that is neither of type "EXCEPTION_TYPE" nor
 * of a subtype of "EXCEPTION_TYPE", the test is considered a failure and further evaluation
 * halts.
 */
#define ASSERT_THROWS(EXPRESSION, EXCEPTION_TYPE) \
    ASSERT_THROWS_WITH_CHECK(EXPRESSION, EXCEPTION_TYPE, ([](const EXCEPTION_TYPE&) {}))

/**
 * Behaves like ASSERT_THROWS, above, but also fails if calling what() on the thrown exception
 * does not return a string equal to EXPECTED_WHAT.
 */
#define ASSERT_THROWS_WHAT(EXPRESSION, EXCEPTION_TYPE, EXPECTED_WHAT)                     \
    ASSERT_THROWS_WITH_CHECK(EXPRESSION, EXCEPTION_TYPE, ([&](const EXCEPTION_TYPE& ex) { \
                                 ASSERT_EQ(::mongo::StringData(ex.what()),                \
                                           ::mongo::StringData(EXPECTED_WHAT));           \
                             }))

/**
 * Behaves like ASSERT_THROWS, above, but also fails if calling getCode() on the thrown exception
 * does not return an error code equal to EXPECTED_CODE.
 */
#define ASSERT_THROWS_CODE(EXPRESSION, EXCEPTION_TYPE, EXPECTED_CODE)                     \
    ASSERT_THROWS_WITH_CHECK(EXPRESSION, EXCEPTION_TYPE, ([&](const EXCEPTION_TYPE& ex) { \
                                 ASSERT_EQ(ex.toStatus().code(), EXPECTED_CODE);          \
                             }))

/**
 * Behaves like ASSERT_THROWS, above, but also fails if calling getCode() on the thrown exception
 * does not return an error code equal to EXPECTED_CODE or if calling what() on the thrown exception
 * does not return a string equal to EXPECTED_WHAT.
 */
#define ASSERT_THROWS_CODE_AND_WHAT(EXPRESSION, EXCEPTION_TYPE, EXPECTED_CODE, EXPECTED_WHAT) \
    ASSERT_THROWS_WITH_CHECK(EXPRESSION, EXCEPTION_TYPE, ([&](const EXCEPTION_TYPE& ex) {     \
                                 ASSERT_EQ(ex.toStatus().code(), EXPECTED_CODE);              \
                                 ASSERT_EQ(::mongo::StringData(ex.what()),                    \
                                           ::mongo::StringData(EXPECTED_WHAT));               \
                             }))


/**
 * Compiles if expr doesn't compile.
 *
 * This only works for compile errors in the "immediate context" of the expression, which matches
 * the rules for SFINAE. The first argument is a defaulted template parameter that is used in the
 * expression to make it dependent. This only works with expressions, not statements, although you
 * can separate multiple expressions with a comma.
 *
 * This should be used at namespace scope, not inside a TEST function.
 *
 * Examples that pass:
 *     ASSERT_DOES_NOT_COMPILE(typename Char = char, *std::declval<Char>());
 *     ASSERT_DOES_NOT_COMPILE(bool B = false, std::enable_if_t<B, int>{});
 *
 * Examples that fail:
 *     ASSERT_DOES_NOT_COMPILE(typename Char = char, *std::declval<Char*>());
 *     ASSERT_DOES_NOT_COMPILE(bool B = true, std::enable_if_t<B, int>{});
 *
 */
#define ASSERT_DOES_NOT_COMPILE(Alias, /*expr*/...) \
    ASSERT_DOES_NOT_COMPILE_1_(                     \
        BOOST_PP_CAT(compileCheck_, __LINE__), Alias, #Alias, (__VA_ARGS__), #__VA_ARGS__)

#define ASSERT_DOES_NOT_COMPILE_1_(Id, Alias, AliasString, Expr, ExprString)        \
    static auto Id(...)->std::true_type;                                            \
    template <Alias>                                                                \
    static auto Id(int)->std::conditional_t<true, std::false_type, decltype(Expr)>; \
    static_assert(decltype(Id(0))::value,                                           \
                  "Expression '" ExprString "' [with " AliasString "] shouldn't compile.");

/**
 * This internal helper is used to ignore warnings about unused results.  Some unit tests which test
 * `ASSERT_THROWS` and its variations are used on functions which both throw and return `Status` or
 * `StatusWith` objects.  Although such function designs are undesirable, they do exist, presently.
 * Therefore this internal helper macro is used by `ASSERT_THROWS` and its variations to silence
 * such warnings without forcing the caller to invoke `.ignore()` on the called function.
 *
 * NOTE: This macro should NOT be used inside regular unit test code to ignore unchecked `Status` or
 * `StatusWith` instances -- if a `Status` or `StatusWith` result is to be ignored, please use the
 * normal `.ignore()` code.  This macro exists only to make using `ASSERT_THROWS` less inconvenient
 * on functions which both throw and return `Status` or `StatusWith`.
 */
#define UNIT_TEST_INTERNALS_IGNORE_UNUSED_RESULT_WARNINGS(EXPRESSION) \
    do {                                                              \
        (void)(EXPRESSION);                                           \
    } while (false)

/**
 * Behaves like ASSERT_THROWS, above, but also calls CHECK(caughtException) which may contain
 * additional assertions.
 */
#define ASSERT_THROWS_WITH_CHECK(EXPRESSION, EXCEPTION_TYPE, CHECK)                              \
    do {                                                                                         \
        try {                                                                                    \
            UNIT_TEST_INTERNALS_IGNORE_UNUSED_RESULT_WARNINGS(EXPRESSION);                       \
        } catch (const EXCEPTION_TYPE& ex) {                                                     \
            CHECK(ex);                                                                           \
            break;                                                                               \
        }                                                                                        \
        /*                                                                                       \
         * Fail outside of the try/catch, this way the code in the `FAIL` macro doesn't have the \
         * potential to throw an exception which we might also be checking for.                  \
         */                                                                                      \
        FAIL("Expected expression " #EXPRESSION " to throw " #EXCEPTION_TYPE                     \
             " but it threw nothing.");                                                          \
    } while (false)

#define ASSERT_STRING_CONTAINS(BIG_STRING, CONTAINS)                                            \
    do {                                                                                        \
        std::string myString(BIG_STRING);                                                       \
        std::string myContains(CONTAINS);                                                       \
        if (myString.find(myContains) == std::string::npos) {                                   \
            ::mongo::str::stream err;                                                           \
            err << "Expected to find " #CONTAINS " (" << myContains << ") in " #BIG_STRING " (" \
                << myString << ")";                                                             \
            ::mongo::unittest::TestAssertionFailure(__FILE__, __LINE__, err).stream();          \
        }                                                                                       \
    } while (false)

#define ASSERT_STRING_OMITS(BIG_STRING, OMITS)                                                  \
    do {                                                                                        \
        std::string myString(BIG_STRING);                                                       \
        std::string myOmits(OMITS);                                                             \
        if (myString.find(myOmits) != std::string::npos) {                                      \
            ::mongo::str::stream err;                                                           \
            err << "Did not expect to find " #OMITS " (" << myOmits << ") in " #BIG_STRING " (" \
                << myString << ")";                                                             \
            ::mongo::unittest::TestAssertionFailure(__FILE__, __LINE__, err).stream();          \
        }                                                                                       \
    } while (false)

/**
 * Construct a single test, named `TEST_NAME` within the test Suite `SUITE_NAME`.
 *
 * Usage:
 *
 * TEST(MyModuleTests, TestThatFooFailsOnErrors) {
 *     ASSERT_EQUALS(error_success, foo(invalidValue));
 * }
 */
#define TEST(SUITE_NAME, TEST_NAME) \
    TEST_(SUITE_NAME, TEST_NAME, UNIT_TEST_DETAIL_TEST_TYPE_NAME(SUITE_NAME, TEST_NAME))

#define TEST_(SUITE_NAME, TEST_NAME, TEST_TYPE)                                           \
    class TEST_TYPE : public ::mongo::unittest::Test {                                    \
    private:                                                                              \
        void _doTest() override;                                                          \
        static inline const RegistrationAgent<TEST_TYPE> _agent{#SUITE_NAME, #TEST_NAME}; \
    };                                                                                    \
    void TEST_TYPE::_doTest()

/**
 * Construct a single test named TEST_NAME that has access to a common class (a "fixture")
 * named "FIXTURE_NAME". FIXTURE_NAME will be the name of the Suite in which the test appears.
 *
 * Usage:
 *
 * class FixtureClass : public mongo::unittest::Test {
 * protected:
 *   int myVar;
 *   void setUp() { myVar = 10; }
 * };
 *
 * TEST(FixtureClass, TestThatUsesFixture) {
 *     ASSERT_EQUALS(10, myVar);
 * }
 */
#define TEST_F(FIXTURE_NAME, TEST_NAME) \
    TEST_F_(FIXTURE_NAME, TEST_NAME, UNIT_TEST_DETAIL_TEST_TYPE_NAME(FIXTURE_NAME, TEST_NAME))

#define TEST_F_(FIXTURE_NAME, TEST_NAME, TEST_TYPE)                                         \
    class TEST_TYPE : public FIXTURE_NAME {                                                 \
    private:                                                                                \
        void _doTest() override;                                                            \
        static inline const RegistrationAgent<TEST_TYPE> _agent{#FIXTURE_NAME, #TEST_NAME}; \
    };                                                                                      \
    void TEST_TYPE::_doTest()

/**
 * Macro to construct a type name for a test, from its `SUITE_NAME` and `TEST_NAME`.
 * Do not use directly in test code.
 */
#define UNIT_TEST_DETAIL_TEST_TYPE_NAME(SUITE_NAME, TEST_NAME) \
    UnitTest_SuiteName##SUITE_NAME##TestName##TEST_NAME

namespace mongo::unittest {

class Result;

void setupTestLogger();

/**
 * Gets a LogstreamBuilder for logging to the unittest log domain, which may have
 * different target from the global log domain.
 */
logger::LogstreamBuilder log();
logger::LogstreamBuilder warning();

/**
 * Representation of a collection of tests.
 *
 * One Suite is constructed for each SUITE_NAME when using the TEST macro.
 *
 * See `OldStyleSuiteSpecification` which adapts dbtests into this framework.
 */
class Suite : public std::enable_shared_from_this<Suite> {
public:
    // do not call directly: use Suite::getSuite(name)
    explicit Suite(std::string name);
    virtual ~Suite();
    Suite(const Suite&) = delete;
    Suite& operator=(const Suite&) = delete;

    void add(std::string name, std::function<void()> testFn);

    std::unique_ptr<Result> run(const std::string& filter, int runsPerTest);

    static int run(const std::vector<std::string>& suites,
                   const std::string& filter,
                   int runsPerTest);

    /**
     * Get a suite with the given name, creating and registering it if necessary.
     *
     * Safe to call during static initialization.
     */
    static Suite& getSuite(const std::string& name);

private:
    struct SuiteTest {
        std::string name;
        std::function<void()> fn;
    };

    std::string _name;
    std::vector<SuiteTest> _tests;
};

/**
 * Adaptor to set up a Suite from a dbtest-style suite.
 * Support for deprecated dbtest-style test suites. Tests are are added by overriding setupTests()
 * in a subclass of OldStyleSuiteSpecification, and defining an OldStyleSuiteInstance<T> object.
 * This approach is
 * deprecated.
 *
 * Example:
 *     class All : public OldStyleSuiteSpecification {
 *     public:
 *         All() : OldStyleSuiteSpecification("BunchaTests") {}
 *         void setupTests() {
 *            add<TestThis>();
 *            add<TestThat>();
 *            add<TestTheOtherThing>();
 *         }
 *     };
 *     OldStyleSuiteInitializer<All> all;
 */
class OldStyleSuiteSpecification {
public:
    struct SuiteTest {
        std::string name;
        std::function<void()> fn;
    };

    OldStyleSuiteSpecification(std::string name) : _name(std::move(name)) {}
    virtual ~OldStyleSuiteSpecification() = default;

    virtual void setupTests() {}

    /**
     * Add an old-style test of type `T` to this Suite, saving any test constructor args
     * that would be needed at test run time.
     * The added test's name will be synthesized as the demangled typename of T.
     * At test run time, the test will be created and run with `T(args...).run()`.
     */
    template <typename T, typename... Args>
    void add(Args&&... args) {
        _tests.push_back({demangleName(typeid(T)), [=] { T(args...).run(); }});
    }

    const std::string& name() const {
        return _name;
    }
    const std::vector<SuiteTest>& tests() const {
        return _tests;
    }

private:
    std::string _name;
    std::vector<SuiteTest> _tests;
};

/**
 * Define a namespace-scope instance of `OldStyleSuiteInitializer<T>` to properly create and
 * initialize an instance of `T` (an `OldStyleSuiteSpecification`). See
 * `OldStyleSuiteSpecification`.
 */
template <typename T>
struct OldStyleSuiteInitializer {
    template <typename... Args>
    explicit OldStyleSuiteInitializer(Args&&... args) {
        T t(std::forward<Args>(args)...);
        init(t);
    }

    void init(OldStyleSuiteSpecification& suiteSpec) const {
        log() << "\t about to setupTests" << std::endl;
        suiteSpec.setupTests();
        log() << "\t done setupTests" << std::endl;
        auto& suite = Suite::getSuite(suiteSpec.name());
        for (auto&& t : suiteSpec.tests()) {
            suite.add(t.name, t.fn);
        }
    }
};


/**
 * Base type for unit test fixtures.  Also, the default fixture type used
 * by the TEST() macro.
 */
class Test {
public:
    Test();
    virtual ~Test();
    Test(const Test&) = delete;
    Test& operator=(const Test&) = delete;

    void run();

    /**
     * Called on the test object before running the test.
     */
    virtual void setUp() {}

    /**
     * Called on the test object after running the test.
     */
    virtual void tearDown() {}

protected:
    /**
     * Adds a Test to a Suite, used by TEST/TEST_F macros.
     */
    template <typename T>
    class RegistrationAgent {
    public:
        RegistrationAgent(std::string suiteName, std::string testName)
            : _suiteName(std::move(suiteName)), _testName(std::move(testName)) {
            Suite::getSuite(_suiteName).add(_testName, [] { T{}.run(); });
        }
        const std::string& getSuiteName() const {
            return _suiteName;
        }
        const std::string& getTestName() const {
            return _testName;
        }
        std::string _suiteName;
        std::string _testName;
    };

    /**
     * This exception class is used to exercise the testing framework itself. If a test
     * case throws it, the framework would not consider it an error.
     */
    class FixtureExceptionForTesting : public std::exception {};

    /**
     * Starts capturing messages logged by code under test.
     *
     * Log messages will still also go to their default destination; this
     * code simply adds an additional sink for log messages.
     *
     * Clears any previously captured log lines.
     */
    void startCapturingLogMessages();

    /**
     * Stops capturing log messages logged by code under test.
     */
    void stopCapturingLogMessages();

    /**
     * Gets a vector of strings, one log line per string, captured since
     * the last call to startCapturingLogMessages() in this test.
     */
    const std::vector<std::string>& getCapturedLogMessages() const {
        return _capturedLogMessages;
    }

    /**
     * Returns the number of collected log lines containing "needle".
     */
    int64_t countLogLinesContaining(const std::string& needle);

    /**
     * Prints the captured log lines.
     */
    void printCapturedLogLines() const;

private:
    /**
     * The test itself.
     */
    virtual void _doTest() = 0;

    bool _isCapturingLogMessages;
    std::vector<std::string> _capturedLogMessages;
    logger::MessageLogDomain::AppenderHandle _captureAppenderHandle;
    std::unique_ptr<logger::MessageLogDomain::EventAppender> _captureAppender;
};

/**
 * Exception thrown when a test assertion fails.
 *
 * Typically thrown by helpers in the TestAssertion class and its ilk, below.
 *
 * NOTE(schwerin): This intentionally does _not_ extend std::exception, so that code under
 * test that (foolishly?) catches std::exception won't swallow test failures.  Doesn't
 * protect you from code that foolishly catches ..., but you do what you can.
 */
class TestAssertionFailureException {
public:
    TestAssertionFailureException(const std::string& theFile,
                                  unsigned theLine,
                                  const std::string& theMessage);

    const std::string& getFile() const {
        return _file;
    }
    unsigned getLine() const {
        return _line;
    }
    const std::string& getMessage() const {
        return _message;
    }
    void setMessage(const std::string& message) {
        _message = message;
    }

    const std::string& what() const {
        return getMessage();
    }

    std::string toString() const;

    const std::string& getStacktrace() const {
        return _stacktrace;
    }

private:
    std::string _file;
    unsigned _line;
    std::string _message;
    std::string _stacktrace;
};

class TestAssertionFailure {
public:
    TestAssertionFailure(const std::string& file, unsigned line, const std::string& message);
    TestAssertionFailure(const TestAssertionFailure& other);
    ~TestAssertionFailure() noexcept(false);

    TestAssertionFailure& operator=(const TestAssertionFailure& other);

    std::ostream& stream();

private:
    TestAssertionFailureException _exception;
    std::ostringstream _stream;
    bool _enabled;
};

enum class ComparisonOp { kEq, kNe, kLt, kLe, kGt, kGe };

template <ComparisonOp op>
class ComparisonAssertion {
private:
    static constexpr auto comparator() {
        if constexpr (op == ComparisonOp::kEq) {
            return std::equal_to<>{};
        } else if constexpr (op == ComparisonOp::kNe) {
            return std::not_equal_to<>{};
        } else if constexpr (op == ComparisonOp::kLt) {
            return std::less<>{};
        } else if constexpr (op == ComparisonOp::kLe) {
            return std::less_equal<>{};
        } else if constexpr (op == ComparisonOp::kGt) {
            return std::greater<>{};
        } else if constexpr (op == ComparisonOp::kGe) {
            return std::greater_equal<>{};
        }
    }

    static constexpr StringData name() {
        if constexpr (op == ComparisonOp::kEq) {
            return "=="_sd;
        } else if constexpr (op == ComparisonOp::kNe) {
            return "!="_sd;
        } else if constexpr (op == ComparisonOp::kLt) {
            return "<"_sd;
        } else if constexpr (op == ComparisonOp::kLe) {
            return "<="_sd;
        } else if constexpr (op == ComparisonOp::kGt) {
            return ">"_sd;
        } else if constexpr (op == ComparisonOp::kGe) {
            return ">="_sd;
        }
    }

    template <typename A, typename B>
    MONGO_COMPILER_NOINLINE ComparisonAssertion(const char* theFile,
                                                unsigned theLine,
                                                StringData aExpression,
                                                StringData bExpression,
                                                const A& a,
                                                const B& b) {
        if (comparator()(a, b)) {
            return;
        }
        std::ostringstream os;
        os << "Expected " << aExpression << " " << name() << " " << bExpression << " (" << a << " "
           << name() << " " << b << ")";
        _assertion = std::make_unique<TestAssertionFailure>(theFile, theLine, os.str());
    }

public:
    // Use a single implementation (identical to the templated one) for all string-like types.
    // This is particularly important to avoid making unique instantiations for each length of
    // string literal.
    static ComparisonAssertion make(const char* theFile,
                                    unsigned theLine,
                                    StringData aExpression,
                                    StringData bExpression,
                                    StringData a,
                                    StringData b);


    // Use a single implementation (identical to the templated one) for all pointer and array types.
    // Note: this is selected instead of the StringData overload for char* and string literals
    // because they are supposed to compare pointers, not contents.
    static ComparisonAssertion make(const char* theFile,
                                    unsigned theLine,
                                    StringData aExpression,
                                    StringData bExpression,
                                    const void* a,
                                    const void* b);
    TEMPLATE(typename A, typename B)
    REQUIRES(!(std::is_convertible_v<A, StringData> && std::is_convertible_v<B, StringData>)&&  //
             !(std::is_pointer_v<A> && std::is_pointer_v<B>)&&                                  //
             !(std::is_array_v<A> && std::is_array_v<B>))
    static ComparisonAssertion make(const char* theFile,
                                    unsigned theLine,
                                    StringData aExpression,
                                    StringData bExpression,
                                    const A& a,
                                    const B& b) {
        return ComparisonAssertion(theFile, theLine, aExpression, bExpression, a, b);
    }

    explicit operator bool() const {
        return static_cast<bool>(_assertion);
    }
    TestAssertionFailure failure() {
        return *_assertion;
    }

private:
    std::unique_ptr<TestAssertionFailure> _assertion;
};

// Explicit instantiation of ComparisonAssertion ctor and factory, for "A OP B".
#define TEMPLATE_COMPARISON_ASSERTION_CTOR_A_OP_B(EXTERN, OP, A, B)             \
    EXTERN template ComparisonAssertion<ComparisonOp::OP>::ComparisonAssertion( \
        const char*, unsigned, StringData, StringData, const A&, const B&);     \
    EXTERN template ComparisonAssertion<ComparisonOp::OP>                       \
    ComparisonAssertion<ComparisonOp::OP>::make(                                \
        const char*, unsigned, StringData, StringData, const A&, const B&);

// Explicit instantiation of ComparisonAssertion ctor and factory for a pair of types.
#define TEMPLATE_COMPARISON_ASSERTION_CTOR_SYMMETRIC(EXTERN, OP, A, B) \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_A_OP_B(EXTERN, OP, A, B)        \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_A_OP_B(EXTERN, OP, B, A)

// Explicit instantiation of ComparisonAssertion ctor and factory for a single type.
#define TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(EXTERN, OP, T) \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_A_OP_B(EXTERN, OP, T, T)

// Call with `extern` to declace extern instantiations, and with no args to explicitly instantiate.
#define INSTANTIATE_COMPARISON_ASSERTION_CTORS(...)                                             \
    __VA_ARGS__ template class ComparisonAssertion<ComparisonOp::kEq>;                          \
    __VA_ARGS__ template class ComparisonAssertion<ComparisonOp::kNe>;                          \
    __VA_ARGS__ template class ComparisonAssertion<ComparisonOp::kGt>;                          \
    __VA_ARGS__ template class ComparisonAssertion<ComparisonOp::kGe>;                          \
    __VA_ARGS__ template class ComparisonAssertion<ComparisonOp::kLt>;                          \
    __VA_ARGS__ template class ComparisonAssertion<ComparisonOp::kLe>;                          \
                                                                                                \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, int)                         \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, long)                        \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, long long)                   \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, unsigned int)                \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, unsigned long)               \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, unsigned long long)          \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, bool)                        \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, double)                      \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, OID)                         \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, BSONType)                    \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, Timestamp)                   \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, Date_t)                      \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, Status)                      \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kEq, ErrorCodes::Error)           \
                                                                                                \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_SYMMETRIC(__VA_ARGS__, kEq, int, long)                   \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_SYMMETRIC(__VA_ARGS__, kEq, int, long long)              \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_SYMMETRIC(__VA_ARGS__, kEq, long, long long)             \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_SYMMETRIC(__VA_ARGS__, kEq, unsigned int, unsigned long) \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_SYMMETRIC(__VA_ARGS__, kEq, Status, ErrorCodes::Error)   \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_SYMMETRIC(__VA_ARGS__, kEq, ErrorCodes::Error, int)      \
                                                                                                \
    /* These are the only types that are often used with ASSERT_NE*/                            \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kNe, Status)                      \
    TEMPLATE_COMPARISON_ASSERTION_CTOR_REFLEXIVE(__VA_ARGS__, kNe, unsigned long)

// Declare that these definitions will be provided in unittest.cpp.
INSTANTIATE_COMPARISON_ASSERTION_CTORS(extern);

/**
 * Get the value out of a StatusWith<T>, or throw an exception if it is not OK.
 */
template <typename T>
const T& assertGet(const StatusWith<T>& swt) {
    ASSERT_OK(swt.getStatus());
    return swt.getValue();
}

template <typename T>
T assertGet(StatusWith<T>&& swt) {
    ASSERT_OK(swt.getStatus());
    return std::move(swt.getValue());
}

/**
 * Return a list of suite names.
 */
std::vector<std::string> getAllSuiteNames();

}  // namespace mongo::unittest
