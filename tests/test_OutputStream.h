#include "evio/OutputStream.h"
#include "evio/Protocol.h"
#include "utils/malloc_size.h"
#include "utils/is_power_of_two.h"
#include "NoEpollDevices.h"
#include <cstdlib>
#include <fcntl.h>

using evio::RefCountReleaser;

TEST(OutputStream, default_output_block_size_c)
{
  evio::Protocol const protocol;
  Dout(dc::notice, "Protocol::minimum_block_size() = " << protocol.minimum_block_size());
  size_t const requested_size = sizeof(evio::MemoryBlock) + protocol.minimum_block_size();
  size_t const alloc_size = utils::malloc_size(requested_size);
  EXPECT_TRUE(utils::is_power_of_two(alloc_size + config::malloc_overhead_c));
  EXPECT_LT(alloc_size, utils::malloc_size(requested_size + 1));
}

class MyOutputDevice : public NoEpollOutputDevice
{
 public:
  evio::OutputBuffer* get_obuffer() const { return m_obuffer; }

  RefCountReleaser stop_output_device()
  {
    int allow_deletion_count = 0;
    evio::OutputDevice::stop_output_device(allow_deletion_count);
    return {this, allow_deletion_count};
  }

  void init(int fd)
  {
    evio::OutputDevice::init(fd);
    state_t::wat state_w(m_state);
    state_w->m_flags.set_regular_file();
  }
};

class MyOutputStream : public evio::OutputStream
{
 private:
  size_t m_mbs;

  size_t minimum_block_size() const override { return m_mbs ? m_mbs : evio::OutputStream::minimum_block_size(); }

 public:
  MyOutputStream(size_t requested_minimum_block_size = 0) : m_mbs(requested_minimum_block_size) { }

  void start_output_device()
  {
    evio::OutputStream::start_output_device();
  }
};

#include "EventLoopFixture.h"

using OutputStreamFixture = EventLoopFixture<testing::Test>;

TEST_F(OutputStreamFixture, create_buffer)
{
  // Create a test OutputDevice.
  auto output_device = evio::create<MyOutputDevice>();

  // Initialize it with some fd.
  char temp_filename[35] = "/tmp/evio_test_XXXXXX_OutputStream";
  int fd = mkostemps(temp_filename, 13, O_CLOEXEC);
  ASSERT_TRUE(fd != -1);
  output_device->init(fd);
  evio::SingleThread type;

  struct args_t
  {
    size_t requested_minimum_block_size;
    size_t buffer_full_watermark;
    size_t max_alloc;
  };
  args_t const test_args = { 64, 256, 1000000 };

  MyOutputStream* output;

  for (int number_of_args = 0; number_of_args < 4; ++number_of_args)
  {
    if (number_of_args == 0)
      output = new MyOutputStream;
    else
      output = new MyOutputStream(test_args.requested_minimum_block_size);

    // Call function under test.
    switch (number_of_args)
    {
      case 0:
        output_device->set_source(*output);
        break;
      case 1:
        output_device->set_source(*output);
        break;
      case 2:
        output_device->set_source(*output, test_args.buffer_full_watermark);
        break;
      case 3:
        output_device->set_source(*output, test_args.buffer_full_watermark, test_args.max_alloc);
        break;
    }

    // Sanity check.
    EXPECT_TRUE(output_device->is_active(type).is_false());

    // Verify that a call to start_output_device now calls m_output_device->start_output_device().
    output->start_output_device();
    EXPECT_TRUE(output_device->is_active(type).is_transitory_true());

    // Likewise when calling stop_output_device().
    RefCountReleaser rcr = output_device->stop_output_device();
    EXPECT_TRUE(output_device->is_active(type).is_false());

    // Check if the expected arguments were passed.
    evio::Protocol const protocol;
    size_t const expected_minimum_block_size = evio::StreamBuf::round_up_minimum_block_size(number_of_args > 0 ? test_args.requested_minimum_block_size : protocol.minimum_block_size());
    ASSERT_EQ(output_device->get_obuffer()->m_minimum_block_size, expected_minimum_block_size);
    size_t const expected_buffer_full_watermark = number_of_args > 1 ? test_args.buffer_full_watermark : 8 * expected_minimum_block_size;
    ASSERT_EQ(output_device->get_obuffer()->m_buffer_full_watermark, expected_buffer_full_watermark);
    size_t const expected_max_allocated_block_size = number_of_args > 2 ? test_args.max_alloc : std::numeric_limits<size_t>::max();
    ASSERT_EQ(output_device->get_obuffer()->m_max_allocated_block_size, expected_max_allocated_block_size);
  }

  // Clean up.
  output_device->close_output_device();
}
