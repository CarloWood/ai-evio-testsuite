#include "sys.h"
#include "gtest.h"
#include "debug.h"

#include "test_set_XXXsockbuf.h"

int main(int argc, char* argv[])
{
  Debug(debug::init());
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
