#include "sys.h"
#include "gtest.h"
#include "debug.h"

#include "test_set_XXXsockbuf.h"
#include "test_print_hostent_on.h"
#include "test_size_of_addr.h"
#include "test_SocketAddress.h"

int main(int argc, char* argv[])
{
  Debug(debug::init());
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::UnitTest::GetInstance()->listeners().Append(new GtestThrowListener);
  return RUN_ALL_TESTS();
}
