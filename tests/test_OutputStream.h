#include "evio/OutputStream.h"
#include "utils/malloc_size.h"
#include "utils/is_power_of_two.h"

TEST(InputDecoder, default_output_blocksize_c) {
  Dout(dc::notice, "default_output_blocksize_c = " << evio::default_output_blocksize_c);
  size_t const requested_size = sizeof(evio::MemoryBlock) + evio::default_output_blocksize_c;
  size_t const alloc_size = utils::malloc_size(requested_size);
  EXPECT_TRUE(utils::is_power_of_two(alloc_size + CW_MALLOC_OVERHEAD));
  EXPECT_LT(alloc_size, utils::malloc_size(requested_size + 1));
}
