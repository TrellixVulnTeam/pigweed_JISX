// Copyright 2019 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy
// of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "pw_preprocessor/concat.h"
#include "pw_preprocessor/util.h"
#include "pw_unit_test/event_handler.h"

#define PW_TEST(test_suite_name, test_name) \
  _PW_TEST(test_suite_name, test_name, ::pw::unit_test::Test)

// TEST() is a pretty generic macro name which could conflict with other code.
// If PW_TEST_DONT_DEFINE_TEST is set, don't alias PW_TEST to TEST.
// GTEST_DONT_DEFINE_TEST is also accepted for compatibility.
#if !PW_TEST_DONT_DEFINE_TEST && !GTEST_DONT_DEFINE_TEST
#define TEST PW_TEST
#endif

#define TEST_F(test_fixture, test_name) \
  _PW_TEST(test_fixture, test_name, test_fixture)

#define EXPECT_EQ(lhs, rhs) _PW_TEST_OP(_PW_TEST_EXPECT, lhs, rhs, ==)
#define EXPECT_NE(lhs, rhs) _PW_TEST_OP(_PW_TEST_EXPECT, lhs, rhs, !=)
#define EXPECT_GT(lhs, rhs) _PW_TEST_OP(_PW_TEST_EXPECT, lhs, rhs, >)
#define EXPECT_GE(lhs, rhs) _PW_TEST_OP(_PW_TEST_EXPECT, lhs, rhs, >=)
#define EXPECT_LT(lhs, rhs) _PW_TEST_OP(_PW_TEST_EXPECT, lhs, rhs, <)
#define EXPECT_LE(lhs, rhs) _PW_TEST_OP(_PW_TEST_EXPECT, lhs, rhs, <=)
#define EXPECT_TRUE(expr) _PW_TEST_TRUE(_PW_TEST_EXPECT, expr)
#define EXPECT_FALSE(expr) _PW_TEST_FALSE(_PW_TEST_EXPECT, expr)
#define EXPECT_STREQ(lhs, rhs) _PW_TEST_STREQ(_PW_TEST_EXPECT, lhs, rhs)
#define EXPECT_STRNE(lhs, rhs) _PW_TEST_STRNE(_PW_TEST_EXPECT, lhs, rhs)

#define ASSERT_EQ(lhs, rhs) _PW_TEST_OP(_PW_TEST_ASSERT, lhs, rhs, ==)
#define ASSERT_NE(lhs, rhs) _PW_TEST_OP(_PW_TEST_ASSERT, lhs, rhs, !=)
#define ASSERT_GT(lhs, rhs) _PW_TEST_OP(_PW_TEST_ASSERT, lhs, rhs, >)
#define ASSERT_GE(lhs, rhs) _PW_TEST_OP(_PW_TEST_ASSERT, lhs, rhs, >=)
#define ASSERT_LT(lhs, rhs) _PW_TEST_OP(_PW_TEST_ASSERT, lhs, rhs, <)
#define ASSERT_LE(lhs, rhs) _PW_TEST_OP(_PW_TEST_ASSERT, lhs, rhs, <=)
#define ASSERT_TRUE(expr) _PW_TEST_TRUE(_PW_TEST_ASSERT, expr)
#define ASSERT_FALSE(expr) _PW_TEST_FALSE(_PW_TEST_ASSERT, expr)
#define ASSERT_STREQ(lhs, rhs) _PW_TEST_STREQ(_PW_TEST_ASSERT, lhs, rhs)
#define ASSERT_STRNE(lhs, rhs) _PW_TEST_STRNE(_PW_TEST_ASSERT, lhs, rhs)

// pw_unit_test framework entry point. Runs every registered test case and
// dispatches the results through the event handler. Returns a status of zero
// if all tests passed, or nonzero if there were any failures.
// This is compatible with Googletest.
//
// In order to receive test output, an event handler must be registered before
// this is called:
//
//   int main() {
//     MyEventHandler handler;
//     pw::unit_test::RegisterEventHandler(&handler);
//     return RUN_ALL_TESTS();
//   }
//
#define RUN_ALL_TESTS() \
  ::pw::unit_test::internal::Framework::Get().RunAllTests()

namespace pw::unit_test {

class Test;

namespace internal {

struct TestInfo;

// Singleton test framework class responsible for managing and running test
// cases. This implementation is internal to Pigweed test; free functions
// wrapping its functionality are exposed as the public interface.
class Framework {
 public:
  constexpr Framework()
      : current_test_(nullptr),
        current_result_(TestResult::kSuccess),
        run_tests_summary_{.passed_tests = 0, .failed_tests = 0},
        exit_status_(0),
        event_handler_(nullptr),
        memory_pool_() {}

  static Framework& Get() { return framework_; }

  // Registers a single test case with the framework. The framework owns the
  // registered unit test. Called during static initialization.
  void RegisterTest(TestInfo* test);

  // Sets the handler to which the framework dispatches test events. During a
  // test run, the framework owns the event handler.
  void RegisterEventHandler(EventHandler* event_handler) {
    event_handler_ = event_handler;
  }

  // Runs all registered test cases, returning a status of 0 if all succeeded or
  // nonzero if there were any failures. Test events that occur during the run
  // are sent to the registered event handler, if any.
  int RunAllTests();

  // Constructs an instance of a unit test class and runs the test.
  //
  // Tests are constructed within a static memory pool at run time instead of
  // being statically allocated to avoid blowing up the size of the test binary
  // in cases where users have large test fixtures (e.g. containing buffers)
  // reused many times. Instead, only a small, fixed-size TestInfo struct is
  // statically allocated per test case, with a run() function that references
  // this method instantiated for its test class.
  template <typename TestInstance>
  static void CreateAndRunTest() {
    // TODO(frolv): Update the assert message with the name of the config option
    // for memory pool size once it is configurable.
    static_assert(
        sizeof(TestInstance) <= sizeof(memory_pool_),
        "The test memory pool is too small for this test. Either increase "
        "kTestMemoryPoolSizeBytes or decrease the size of your test fixture.");

    Framework& framework = Get();

    // Construct the test object within the static memory pool.
    TestInstance* test_instance = new (&framework.memory_pool_) TestInstance;

    framework.StartTest(test_instance);
    test_instance->PigweedTestRun();
    framework.EndTest(test_instance);

    // Manually call the destructor as it is not called automatically for
    // objects constructed using placement new.
    test_instance->~TestInstance();
  }

  // Runs an expectation function for the currently active test case.
  template <typename Expectation, typename Lhs, typename Rhs>
  bool CurrentTestExpect(Expectation expectation,
                         const Lhs& lhs,
                         const Rhs& rhs,
                         const char* expression,
                         int line) {
    bool result = expectation(lhs, rhs);
    ExpectationResult(expression, line, result);
    return result;
  }

 private:
  // Dispatches an event indicating that a test started running.
  void StartTest(Test* test);

  // Dispatches an event indicating that a test finished running.
  void EndTest(Test* test);

  // Dispatches an event indicating the result of an expectation.
  void ExpectationResult(const char* expression, int line, bool success);

  // Singleton instance of the framework class.
  static Framework framework_;

  // Linked list of all registered test cases. This is static as it tests are
  // registered using static initialization.
  static TestInfo* tests_;

  // The current test case which is running.
  Test* current_test_;

  // Overall result of the current test case (pass/fail).
  TestResult current_result_;

  // Overall result of the ongoing test run, which covers multiple tests.
  RunTestsSummary run_tests_summary_;

  // Program exit status returned by RunAllTests for Googletest compatibility.
  int exit_status_;

  // Handler to which to dispatch test events.
  EventHandler* event_handler_;

  // Memory region in which to construct test case classes as they are run.
  // TODO(frolv): Make the memory pool size configurable.
  static constexpr size_t kTestMemoryPoolSizeBytes = 8192;
  std::aligned_storage_t<kTestMemoryPoolSizeBytes, alignof(std::max_align_t)>
      memory_pool_;
};

// Information about a single test case, including a pointer to a function which
// constructs and runs the test class. These are statically allocated instead of
// the test classes, as test classes can be very large.
struct TestInfo {
  TestInfo(const char* const test_suite_name,
           const char* const test_name,
           const char* const file_name,
           void (*run)())
      : test_suite_name(test_suite_name),
        test_name(test_name),
        file_name(file_name),
        run(run) {
    Framework::Get().RegisterTest(this);
  }

  // Name of the suite to which the test case belongs.
  const char* const test_suite_name;

  // Name of the test case itself.
  const char* const test_name;

  // Path to the file in which the test case is located.
  const char* const file_name;

  // Function which runs the test case. Refers to Framework::CreateAndRunTest
  // instantiated for the test case's class.
  void (*run)();

  // TestInfo structs are registered with the test framework and stored as a
  // linked list.
  TestInfo* next = nullptr;
};

}  // namespace internal

// Base class for all test cases or custom test fixtures.
// Every unit test created using the TEST or TEST_F macro defines a class that
// inherits from this (or a subclass of this).
//
// For example, given the following test definition:
//
//   TEST(MyTest, SaysHello) {
//     ASSERT_STREQ(SayHello(), "Hello, world!");
//   }
//
// A new class is defined for the test, e.g. MyTest_SaysHello_Test. This class
// inherits from the Test class and implements its PigweedTestBody function with
// the block provided to the TEST macro.
class Test {
 public:
  // Runs the unit test. Currently, this simply executes the test body, but it
  // could be expanded to perform more bookkeeping operations.
  void PigweedTestRun() { PigweedTestBody(); }
  virtual ~Test() = default;

 protected:
  // Called by subclasses' constructors with their TestInfo instances.
  void PigweedSetTestInfo(const internal::TestInfo* test_info) {
    pigweed_test_info_ = test_info;
  }

 private:
  friend class internal::Framework;

  // Pointer to the TestInfo struct statically allocated for the test case.
  const internal::TestInfo* pigweed_test_info_;

  // The user-provided body of the test case. Populated by the TEST macro.
  virtual void PigweedTestBody() = 0;
};

}  // namespace pw::unit_test

#define _PW_TEST_CLASS_NAME(test_suite_name, test_name) \
  PW_CONCAT(test_suite_name, _, test_name, _Test)

#define _PW_TEST(test_suite_name, test_name, parent_class)         \
  static_assert(sizeof(PW_STRINGIFY(test_suite_name)) > 1,         \
                "test_suite_name must not be empty");              \
  static_assert(sizeof(PW_STRINGIFY(test_name)) > 1,               \
                "test_name must not be empty");                    \
                                                                   \
  class _PW_TEST_CLASS_NAME(test_suite_name, test_name) final      \
      : public parent_class {                                      \
   public:                                                         \
    _PW_TEST_CLASS_NAME(test_suite_name, test_name)() {            \
      PigweedSetTestInfo(&test_info_);                             \
    }                                                              \
                                                                   \
   private:                                                        \
    void PigweedTestBody() override;                               \
    static ::pw::unit_test::internal::TestInfo test_info_;         \
  };                                                               \
                                                                   \
  ::pw::unit_test::internal::TestInfo                              \
      _PW_TEST_CLASS_NAME(test_suite_name, test_name)::test_info_( \
          PW_STRINGIFY(test_suite_name),                           \
          PW_STRINGIFY(test_name),                                 \
          __FILE__,                                                \
          ::pw::unit_test::internal::Framework::CreateAndRunTest<  \
              _PW_TEST_CLASS_NAME(test_suite_name, test_name)>);   \
                                                                   \
  void _PW_TEST_CLASS_NAME(test_suite_name, test_name)::PigweedTestBody()

#define _PW_TEST_EXPECT(lhs, rhs, expectation, expectation_string) \
  ::pw::unit_test::internal::Framework::Get().CurrentTestExpect(   \
      expectation,                                                 \
      (lhs),                                                       \
      (rhs),                                                       \
      #lhs " " expectation_string " " #rhs,                        \
      __LINE__)

#define _PW_TEST_ASSERT(lhs, rhs, expectation, expectation_string)   \
  if (!_PW_TEST_EXPECT(lhs, rhs, expectation, expectation_string)) { \
    return;                                                          \
  }

#define _PW_TEST_OP(expect_or_assert, lhs, rhs, op) \
  expect_or_assert(                                 \
      lhs, rhs, [](const auto& l, const auto& r) { return l op r; }, #op)

#define _PW_TEST_TRUE(expect_or_assert, expr)                              \
  expect_or_assert(                                                        \
      expr,                                                                \
      true,                                                                \
      [](const auto& arg, const auto&) { return static_cast<bool>(arg); }, \
      "is")

#define _PW_TEST_FALSE(expect_or_assert, expr)                              \
  expect_or_assert(                                                         \
      expr,                                                                 \
      false,                                                                \
      [](const auto& arg, const auto&) { return !static_cast<bool>(arg); }, \
      "is")

#define _PW_TEST_STREQ(expect_or_assert, lhs, rhs)                         \
  expect_or_assert(                                                        \
      lhs,                                                                 \
      rhs,                                                                 \
      [](const auto& l, const auto& r) { return std::strcmp(l, r) == 0; }, \
      "equals")

#define _PW_TEST_STRNE(expect_or_assert, lhs, rhs)                         \
  expect_or_assert(                                                        \
      lhs,                                                                 \
      rhs,                                                                 \
      [](const auto& l, const auto& r) { return std::strcmp(l, r) != 0; }, \
      "does not equal")

// Alias Test as ::testing::Test for Googletest compatibility.
namespace testing {
using Test = ::pw::unit_test::Test;
}  // namespace testing
