#include "evio/InputDecoder.h"
#include "utils/malloc_size.h"
#include "utils/is_power_of_two.h"
#ifdef CWDEBUG
#include "libcwd/buf2str.h"
#endif
#include <cstdlib>
#include <fcntl.h>      // O_CLOEXEC

TEST(InputDecoder, default_input_blocksize_c)
{
  Dout(dc::notice, "default_input_blocksize_c = " << evio::default_input_blocksize_c);
  size_t const requested_size = sizeof(evio::MemoryBlock) + evio::default_input_blocksize_c;
  size_t const alloc_size = utils::malloc_size(requested_size);
  EXPECT_TRUE(utils::is_power_of_two(alloc_size + CW_MALLOC_OVERHEAD));
  EXPECT_LT(alloc_size, utils::malloc_size(requested_size + 1));
}

class MyInputDevice : public evio::InputDevice
{
 public:
  evio::InputBuffer* get_ibuffer() const { return m_ibuffer; }

  void init(int fd)
  {
    evio::InputDevice::init(fd);
  }
};

class MyInputDecoder : public evio::InputDecoder
{
 public:
  using evio::InputDecoder::InputDecoder;

  evio::RefCountReleaser decode(evio::MsgBlock&& CWDEBUG_ONLY(msg)) override
  {
    DoutEntering(dc::notice, "MyInputDecoder::decode(" NAD_DoutEntering_ARG "\"" << libcwd::buf2str(msg.get_start(), msg.get_size()) << "\")");
  }

  void start_input_device()
  {
    evio::InputDecoder::start_input_device();
  }

  void stop_input_device()
  {
    evio::InputDecoder::stop_input_device();
  }

  size_t end_of_msg_finder(char const* new_data, size_t rlen) override
  {
    return evio::InputDecoder::end_of_msg_finder(new_data, rlen);
  }
};

TEST(InputDecoder, create_buffer)
{
  // Create a test InputDevice.
  auto input_device = evio::create<MyInputDevice>();

  // Initialize it with some fd.
  char temp_filename[35] = "/tmp/evio_test_XXXXXX_InputDecoder";
  int fd = mkostemps(temp_filename, 13, O_CLOEXEC);
  ASSERT_TRUE(fd != -1);
  input_device->init(fd);
  evio::SingleThread type;

  MyInputDecoder decoder;

  struct args_t
  {
    size_t minimum_blocksize;
    size_t buffer_full_watermark;
    size_t max_alloc;
  };
  args_t const test_args = { 64, 256, 1000000 };

  for (int number_of_args = 0; number_of_args < 4; ++number_of_args)
  {
    // Call function under test.
    switch (number_of_args)
    {
      case 0:
        input_device->input(decoder);
        break;
      case 1:
        input_device->input(decoder, test_args.minimum_blocksize);
        break;
      case 2:
        input_device->input(decoder, test_args.minimum_blocksize, test_args.buffer_full_watermark);
        break;
      case 3:
        input_device->input(decoder, test_args.minimum_blocksize, test_args.buffer_full_watermark, test_args.max_alloc);
        break;
    }

    // Sanity check.
    EXPECT_TRUE(input_device->is_active(type).is_false());

    // Verify that a call to start_input_device now calls m_input_device->start_input_device().
    decoder.start_input_device();
    EXPECT_TRUE(input_device->is_active(type).is_transitory_true());

    // Likewise when calling stop_input_device().
    decoder.stop_input_device();
    EXPECT_TRUE(input_device->is_active(type).is_false());

    // Check if the expected arguments were passed.
    size_t const expected_minimum_block_size = number_of_args > 0 ? test_args.minimum_blocksize : evio::default_input_blocksize_c;
    ASSERT_EQ(input_device->get_ibuffer()->m_minimum_block_size, expected_minimum_block_size);
    size_t const expected_buffer_full_watermark = number_of_args > 1 ? test_args.buffer_full_watermark : 8 * expected_minimum_block_size;
    ASSERT_EQ(input_device->get_ibuffer()->m_buffer_full_watermark, expected_buffer_full_watermark);
    size_t const expected_max_allocated_block_size = number_of_args > 2 ? test_args.max_alloc : std::numeric_limits<size_t>::max();
    ASSERT_EQ(input_device->get_ibuffer()->m_max_allocated_block_size, expected_max_allocated_block_size);
  }

  // Clean up.
  input_device->close_input_device();
}

TEST(InputDecoder, end_of_msg_finder)
{
  MyInputDecoder decoder;

  char const* test_msgs[] = {
    "",
    "\n",
    "X",
    "X\n",
    "X\n\n",
    "X\nY",
    "XY",
    "A somewhat longer message without a newline at the end.",
    "A somewhat longer message with a newline at the end.\n",
    "A somewhat longer message with a newline at the end.\nAnd trailing characters.",
    "\nA somewhat longer message without a newline at the end.",
  };

  for (int i = 0; i < ssizeof(test_msgs) / ssizeof(char const*); ++i)
  {
    size_t rlen = std::strlen(test_msgs[i]);
    size_t len = decoder.end_of_msg_finder(test_msgs[i], rlen);
    size_t pos;
    if ((pos = std::string(test_msgs[i]).find('\n')) == std::string::npos)
      EXPECT_EQ(len, 0UL);
    else
      EXPECT_EQ(len, pos + 1);
  }
}
