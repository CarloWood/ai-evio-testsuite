#include "evio/protocol/MessageLengthInterface.h"
#include "utils/malloc_size.h"
#include "utils/is_power_of_two.h"
#ifdef CWDEBUG
#include "libcwd/buf2str.h"
#endif
#include "NoEpollDevices.h"
#include <cstdlib>
#include <fcntl.h>      // O_CLOEXEC

using evio::RefCountReleaser;

TEST(Decoder, default_minimum_block_size)
{
  evio::protocol::MessageLengthInterface const message_length_interface;
  Dout(dc::notice, "MessageLengthInterface::minimum_block_size() = " << message_length_interface.minimum_block_size());
  size_t const requested_size = sizeof(evio::MemoryBlock) + message_length_interface.minimum_block_size();
  size_t const alloc_size = utils::malloc_size(requested_size);
  EXPECT_TRUE(utils::is_power_of_two(alloc_size + config::malloc_overhead_c));
  EXPECT_LT(alloc_size, utils::malloc_size(requested_size + 1));
}

class MyInputDevice : public NoEpollInputDevice
{
 public:
  evio::InputBuffer* get_ibuffer() const { return m_ibuffer; }

  void init(int fd)
  {
    evio::InputDevice::fd_init(fd);
    state_t::wat state_w(m_state);
    state_w->m_flags.set_regular_file();
  }

  void reset()
  {
    if (m_ibuffer->release(CWDEBUG_ONLY(this)))
      m_ibuffer = nullptr;
  }
};

class MyDecoder : public evio::protocol::Decoder
{
 private:
  size_t m_mbs;

  size_t minimum_block_size() const override { return m_mbs ? m_mbs : evio::protocol::Decoder::minimum_block_size(); }

 public:
  MyDecoder(size_t minimum_block_size = 0) : m_mbs(minimum_block_size) { }

 public: // Hack public access.
  void start_input_device()
  {
    evio::protocol::Decoder::start_input_device();
  }

  void stop_input_device()
  {
    evio::protocol::Decoder::stop_input_device();
  }

  size_t end_of_msg_finder(char const* new_data, size_t rlen, evio::EndOfMsgFinderResult& result) override
  {
    return evio::protocol::Decoder::end_of_msg_finder(new_data, rlen, result);
  }

 public:
  void decode(int& CWDEBUG_ONLY(allow_deletion_count), evio::MsgBlock&& CWDEBUG_ONLY(msg)) override
  {
    DoutEntering(dc::notice, "MyDecoder::decode({" << allow_deletion_count << "}, \"" << libcwd::buf2str(msg.get_start(), msg.get_size()) << "\")");
  }
};

#include "EventLoopFixture.h"

using DecoderFixture = EventLoopFixture<testing::Test>;

TEST_F(DecoderFixture, create_buffer)
{
  // Create a test InputDevice.
  auto input_device = evio::create<MyInputDevice>();

  // Initialize it with some fd.
  char temp_filename[30] = "/tmp/evio_test_XXXXXX_Decoder";
  int fd = mkostemps(temp_filename, 8, O_CLOEXEC);
  ASSERT_TRUE(fd != -1);
  input_device->init(fd);
  evio::SingleThread type;

  struct args_t
  {
    size_t requested_minimum_block_size;
    size_t buffer_full_watermark;
    size_t max_alloc;
  };
  args_t const test_args = { 64, 256, 1000000 };

  for (int number_of_args = 0; number_of_args < 4; ++number_of_args)
  {
    // Call function under test.
    MyDecoder* decoder;
    if (number_of_args == 0)
      decoder = new MyDecoder;
    else
      decoder = new MyDecoder(test_args.requested_minimum_block_size);
    switch (number_of_args)
    {
      case 0:
        input_device->set_protocol_decoder(*decoder);
        break;
      case 1:
        input_device->set_protocol_decoder(*decoder);
        break;
      case 2:
        input_device->set_protocol_decoder(*decoder, test_args.buffer_full_watermark);
        break;
      case 3:
        input_device->set_protocol_decoder(*decoder, test_args.buffer_full_watermark, test_args.max_alloc);
        break;
    }

    // Sanity check.
    EXPECT_TRUE(input_device->is_active(type).is_false());

    // Verify that a call to start_input_device now calls m_input_device->start_input_device().
    decoder->start_input_device();
    EXPECT_TRUE(input_device->is_active(type).is_transitory_true());

    // Likewise when calling stop_input_device().
    decoder->stop_input_device();
    EXPECT_TRUE(input_device->is_active(type).is_false());

    // Check if the expected arguments were passed.
    evio::protocol::MessageLengthInterface const message_length_interface;
    size_t const expected_minimum_block_size =
      number_of_args > 0 ?
        evio::StreamBuf::round_up_minimum_block_size(test_args.requested_minimum_block_size) :       // See what the StreamBuf constructor passes to StreamBufProducer.
        evio::StreamBuf::round_up_minimum_block_size(message_length_interface.minimum_block_size());
    ASSERT_EQ(input_device->get_ibuffer()->m_minimum_block_size, expected_minimum_block_size);
    size_t const expected_buffer_full_watermark = number_of_args > 1 ? test_args.buffer_full_watermark : 8 * expected_minimum_block_size;
    ASSERT_EQ(input_device->get_ibuffer()->m_buffer_full_watermark, expected_buffer_full_watermark);
    size_t const expected_max_allocated_block_size = number_of_args > 2 ? test_args.max_alloc : std::numeric_limits<size_t>::max();
    ASSERT_EQ(input_device->get_ibuffer()->m_max_allocated_block_size, expected_max_allocated_block_size);

    delete decoder;

    input_device->reset();
  }

  // Clean up.
  input_device->close_input_device();
}

TEST_F(DecoderFixture, end_of_msg_finder)
{
  MyDecoder decoder;

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
    evio::EndOfMsgFinderResult result;
    size_t len = decoder.end_of_msg_finder(test_msgs[i], rlen, result);
    size_t pos;
    if ((pos = std::string(test_msgs[i]).find('\n')) == std::string::npos)
      EXPECT_EQ(len, 0UL);
    else
      EXPECT_EQ(len, pos + 1);
  }
}
