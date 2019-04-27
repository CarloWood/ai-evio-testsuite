#include "evio/InputDecoder.h"
#include "utils/malloc_size.h"
#include "utils/is_power_of_two.h"
#include "libcwd/buf2str.h"
#include <cstdlib>
#include <fcntl.h>      // O_CLOEXEC

TEST(InputDecoder, default_input_blocksize_c) {
  Dout(dc::notice, "default_input_blocksize_c = " << evio::default_input_blocksize_c);
  size_t const requested_size = sizeof(evio::MemoryBlock) + evio::default_input_blocksize_c;
  size_t const alloc_size = utils::malloc_size(requested_size);
  EXPECT_TRUE(utils::is_power_of_two(alloc_size + CW_MALLOC_OVERHEAD));
  EXPECT_LT(alloc_size, utils::malloc_size(requested_size + 1));
}

class MyInputDevice : public evio::InputDevice
{
 protected:
  void start_input_device(evio::GetThread) { Dout(dc::notice, "Calling MyInputDevice::start_input_device()"); }
  evio::RefCountReleaser stop_input_device() { Dout(dc::notice, "Calling MyInputDevice::stop_input_device()"); return {}; }
};

class MyInputDecoder : public evio::InputDecoder
{
 private:
   boost::intrusive_ptr<MyInputDevice> m_input_device;

 public:
  using evio::InputDecoder::InputDecoder;

  evio::RefCountReleaser decode(evio::MsgBlock&& msg, evio::GetThread type) override
  {
    DoutEntering(dc::notice, "MyInputDecoder::decode(\"" << libcwd::buf2str(msg.get_start(), msg.get_size()) << "\", type)");
    return {};
  }

  void test_create_buffer()
  {
    // Create a test InputDevice.
    m_input_device = evio::create<MyInputDevice>();

    // Initialize it with some fd.
    char temp_filename[35] = "/tmp/evio_test_XXXXXX_InputDecoder";
    int fd = mkostemps(temp_filename, 13, O_CLOEXEC);
    ASSERT_TRUE(fd != -1);
    m_input_device->init(fd);
    evio::SingleThread type;

    // Call function under test.
    m_input_device->input(*this);

    // Sanity check.
    EXPECT_TRUE(m_input_device->is_active(type).is_false());

    // Verify that a call to start_input_device now calls m_input_device->start_input_device(type).
    start_input_device(type);
    EXPECT_TRUE(m_input_device->is_active(type).is_transitory_true());

    // Likewise when calling stop_input_device().
    evio::RefCountReleaser allow_deletion = stop_input_device();
    EXPECT_TRUE(m_input_device->is_active(type).is_false());

    // Clean up.
    m_input_device->close_input_device();
  }
};

TEST(InputDecoder, create_buffer) {
  MyInputDecoder decoder;
  CALL(decoder.test_create_buffer());
}
