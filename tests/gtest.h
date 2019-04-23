#define GTEST_DONT_DEFINE_TEST 1

#include "gtest/gtest.h"

// See https://github.com/google/googletest/blob/master/googletest/docs/advanced.md
class GtestThrowListener : public testing::EmptyTestEventListener
{
  void OnTestPartResult(testing::TestPartResult const& result) override
  {
    if (result.type() == testing::TestPartResult::kFatalFailure)
      throw testing::AssertionException(result);
  }
};

#include "gmock/gmock.h"

// Definition of my own macros.

#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#include <sstream>

#define CW_GTEST_TEST_NAME(test_suite_name, test_name) \
  cw_##test_suite_name##_##test_name##_test

#define TEST(test_suite_name, test_name) \
    extern void CW_GTEST_TEST_NAME(test_suite_name, test_name)(); \
    GTEST_TEST(test_suite_name, test_name) \
    { \
      try \
      { \
        CW_GTEST_TEST_NAME(test_suite_name, test_name)(); \
      } \
      catch (AIAlert::Error const& gtest_error) \
      { \
        FAIL() << "Unexpected exception of type `AIAlert::Error':\n" << gtest_error; \
      } \
    } \
    void CW_GTEST_TEST_NAME(test_suite_name, test_name)()

#define PREFIX_AIALERT_(prefix, statement, regex) \
    do \
    { \
      try \
      { \
        statement; \
      } \
      catch (AIAlert::Error const& gtest_error) \
      { \
        std::ostringstream gtest_ss; \
        gtest_ss << gtest_error; \
        prefix##_THAT(gtest_ss.str(), ContainsRegex(regex)); \
      } \
    } \
    while(0)

#define EXPECT_AIALERT(statement, regex) \
    PREFIX_AIALERT_(EXPECT, statement, regex)

#define ASSERT_AIALERT(statement, regex) \
    PREFIX_AIALERT_(ASSERT, statement, regex)

#define CALL(subroutine) \
    do { SCOPED_TRACE("Called from here"); subroutine; } while(0)

#include "debug.h"

extern bool g_debug_output_on;

class DebugDeathTest
{
 private:
  std::ostream* m_old_device;

 public:
  DebugDeathTest()
  {
    m_old_device = libcwd::libcw_do.get_ostream();
    libcwd::libcw_do.set_ostream(&std::cerr);
  }
  ~DebugDeathTest()
  {
    libcwd::libcw_do.set_ostream(m_old_device);
  }
};

#undef EXPECT_DEATH
#define EXPECT_DEATH(statement, regex) \
    do \
    { \
      DebugDeathTest __dummy; \
      EXPECT_EXIT(statement, ::testing::internal::ExitedUnsuccessfully, regex); \
    } \
    while(0)
