#include "sys.h"
#include "gtest/gtest.h"
#include "debug.h"

#include <cmath>

double square_root(double n)
{
  DoutEntering(dc::notice, "square_root(" << n << ")");
  return n < 0 ? -1 : std::sqrt(n);
}

TEST (SquareRootTest, PositiveNos) {
  EXPECT_EQ (18.0, square_root(324.0));
    EXPECT_EQ (25.4, square_root(645.16));
    EXPECT_EQ (50.332, square_root(2533.310224));
}

TEST (SquareRootTest, ZeroAndNegativeNos) {
    ASSERT_EQ (0.0, square_root(0.0));
    ASSERT_EQ (-1, square_root(-22.0));
}

int main(int argc, char* argv[])
{
  Debug(debug::init());
  Debug(dc::notice.off());
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
