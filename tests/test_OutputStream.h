#include "evio/OutputStream.h"
#include "utils/malloc_size.h"
#include "utils/is_power_of_two.h"

TEST(OutputStream, default_output_blocksize_c) {
  Dout(dc::notice, "default_output_blocksize_c = " << evio::default_output_blocksize_c);
  size_t const requested_size = sizeof(evio::MemoryBlock) + evio::default_output_blocksize_c;
  size_t const alloc_size = utils::malloc_size(requested_size);
  EXPECT_TRUE(utils::is_power_of_two(alloc_size + CW_MALLOC_OVERHEAD));
  EXPECT_LT(alloc_size, utils::malloc_size(requested_size + 1));
}

class MyOutputDevice : public evio::OutputDevice
{
 public:
  evio::OutputBuffer* get_obuffer() const { return m_obuffer; }

  evio::RefCountReleaser stop_output_device()
  {
    return evio::OutputDevice::stop_output_device();
  }
};

class MyOutputStream : public evio::OutputStream
{
 public:
  using evio::OutputStream::OutputStream;

  void start_output_device(evio::PutThread type)
  {
    evio::OutputStream::start_output_device(type);
  }
};

TEST(OutputStream, create_buffer) {
  // Create a test OutputDevice.
  auto output_device = evio::create<MyOutputDevice>();

  // Initialize it with some fd.
  char temp_filename[35] = "/tmp/evio_test_XXXXXX_OutputStream";
  int fd = mkostemps(temp_filename, 13, O_CLOEXEC);
  ASSERT_TRUE(fd != -1);
  output_device->init(fd);
  evio::SingleThread type;

  MyOutputStream output;

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
        output_device->output(output);
        break;
      case 1:
        output_device->output(output, test_args.minimum_blocksize);
        break;
      case 2:
        output_device->output(output, test_args.minimum_blocksize, test_args.buffer_full_watermark);
        break;
      case 3:
        output_device->output(output, test_args.minimum_blocksize, test_args.buffer_full_watermark, test_args.max_alloc);
        break;
    }

    // Sanity check.
    EXPECT_TRUE(output_device->is_active(type).is_false());

    // Verify that a call to start_output_device now calls m_output_device->start_output_device(type).
    output.start_output_device(type);
    EXPECT_TRUE(output_device->is_active(type).is_transitory_true());

#if 0
    // Likewise when calling stop_output_device().
    evio::RefCountReleaser allow_deletion = decoder.stop_output_device();
    EXPECT_TRUE(output_device->is_active(type).is_false());
#else
    output_device->stop_output_device();
#endif

    // Check if the expected arguments were passed.
    {
      std::stringstream ss;
      output_device->get_obuffer()->printOn(ss);
      std::string line;
      // Expected output starts with:
      // minimum_block_size = 480; max_allocated_block_size = 18446744073709551615; buffer_full_watermark = 3840; current_number_of_blocks = 1
      bool success_reading_next_line;
      while ((success_reading_next_line = static_cast<bool>(std::getline(ss, line))))
        if (line.substr(0, 18) == "minimum_block_size")
          break;
      ASSERT_TRUE(success_reading_next_line);
      std::vector<size_t> numbers;
      size_t pos = 0;
      for (int n = 0; n < 3; ++n)
      {
        pos = line.find('=', pos) + 1;
        size_t val = std::stoul(line.substr(pos));
        numbers.push_back(val);
      }
      ASSERT_EQ(numbers.size(), 3UL);
      ASSERT_EQ(numbers[0], number_of_args > 0 ? test_args.minimum_blocksize : evio::default_output_blocksize_c);
      ASSERT_EQ(numbers[1], number_of_args > 1 ? test_args.buffer_full_watermark : 8 * numbers[0]);
      ASSERT_EQ(numbers[2], number_of_args > 2 ? test_args.max_alloc : std::numeric_limits<size_t>::max());
    }
  }

  // Clean up.
  output_device->close_output_device();
}
