#define GTEST_DONT_DEFINE_ASSERT_EQ 1
#define GTEST_DONT_DEFINE_ASSERT_NE 1
#define GTEST_DONT_DEFINE_ASSERT_LE 1
#define GTEST_DONT_DEFINE_ASSERT_LT 1
#define GTEST_DONT_DEFINE_ASSERT_GE 1
#define GTEST_DONT_DEFINE_ASSERT_GT 1

#define MY_ASSERT_PRED_FORMAT2(pred_format, v1, v2) \
    if (::testing::AssertionResult const gtest_ar = pred_format(#v1, #v2, v1, v2)) \
      ; \
    else \
      throw testing::internal::MyAssertHelper(__FILE__, __LINE__, gtest_ar.failure_message()) = ::testing::Message()

#define ASSERT_EQ(val1, val2) \
  MY_ASSERT_PRED_FORMAT2(::testing::internal::EqHelper<GTEST_IS_NULL_LITERAL_(val1)>::Compare, val1, val2)
#define ASSERT_NE(val1, val2) \
  MY_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperNE, val1, val2)
#define ASSERT_LE(val1, val2) \
  MY_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperLE, val1, val2)
#define ASSERT_LT(val1, val2) \
  MY_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperLT, val1, val2)
#define ASSERT_GE(val1, val2) \
  MY_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperGE, val1, val2)
#define ASSERT_GT(val1, val2) \
  MY_ASSERT_PRED_FORMAT2(::testing::internal::CmpHelperGT, val1, val2)

#include "gtest/gtest.h"

namespace testing {
namespace internal {

struct AssertException
{
};

struct MyAssertHelper
{
  char const* m_file;
  int m_line;
  char const* m_message;

  MyAssertHelper(char const* file, int line, char const* message) : m_file(file), m_line(line), m_message(message) { }

  AssertException operator=(::testing::Message const& message) const
  {
    ::testing::internal::AssertHelper(::testing::TestPartResult::kFatalFailure, m_file, m_line, m_message) = message;
    return {};
  }
};

} // namespace internal
} // namespace testing

// Definition of my own macros.

#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#define ASSERT_NO_AIALERT_BEGIN \
    do \
    { \
      try \
      {

#define ASSERT_NO_AIALERT_END \
      } \
      catch (::testing::internal::AssertException const&) \
      { \
      } \
      catch (AIAlert::Error const& error) \
      { \
        FAIL() << "Unexpected exception of type `AIAlert::Error':\n" << error; \
      } \
    } while(0)
