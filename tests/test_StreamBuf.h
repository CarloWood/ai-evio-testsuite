#include "evio/InputDecoder.h"
#include "evio/StreamBuf.h"
#include "utils/RandomStream.h"
#include "utils/StreamHasher.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

TEST(StreamBuf, TypeDefs) {

  // Typedefs.
  using StreamBuf = evio::StreamBuf;
  EXPECT_TRUE((std::is_same<StreamBuf::char_type, char>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::traits_type, std::char_traits<char>>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::int_type, int>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::pos_type, std::streampos>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::off_type, std::streamoff>::value));
}

class MyOutputDevice : public evio::OutputDevice
{
 private:
  bool m_sync_called;

 public:
  evio::OutputBuffer* get_obuffer() const { return m_obuffer; }

  evio::RefCountReleaser stop_output_device()
  {
    return evio::OutputDevice::stop_output_device();
  }

 protected:
  int sync() override { m_sync_called = true; return evio::OutputDevice::sync(); }

 public:
  void reset_sync_called() { m_sync_called = false; }
  bool sync_called() const { return m_sync_called; }
  void set_dont_close() { m_flags |= INTERNAL_FDS_DONT_CLOSE; }
};

class OutputBufferFixture : public testing::Test
{
 protected:
  boost::intrusive_ptr<MyOutputDevice> m_output_device;
  evio::OutputStream m_output;
  evio::OutputBuffer* m_buffer;
  size_t m_min_block_size;

  evio::GetThread const get_type{};
  evio::PutThread const put_type{};

  OutputBufferFixture() { }

  void SetUp()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v OutputBufferFixture::SetUp()");
    debug::Mark setup;
#endif

    // Create a test OutputDevice.
    m_output_device = evio::create<MyOutputDevice>();
    m_output_device->init(1);           // Otherwise the device is not 'writable', which has influence on certain buffer functions (ie, sync()).
    m_output_device->set_dont_close();
    // Create an OutputBuffer for it.
    m_output_device->output(m_output, 32);
    m_buffer = m_output_device->get_obuffer();
    // Calculate the actual minimum block size, see StreamBuf::StreamBuf.
    m_min_block_size = utils::malloc_size(m_buffer->m_minimum_block_size + sizeof(evio::MemoryBlock)) - sizeof(evio::MemoryBlock);
  }

  void TearDown()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v OutputBufferFixture::TearDown()");
    debug::Mark setup;
#endif

    m_output_device.reset();
    // m_output can be reused (the call to output() above will effectively reset it).
  }

  void test_size(size_t actual_size)
  {
    {
      evio::StreamBuf::GetThreadLock::crat get_area_rat(m_buffer->get_area_lock(get_type));
      evio::StreamBuf::PutThreadLock::crat put_area_rat(m_buffer->put_area_lock(put_type));
      EXPECT_EQ(m_buffer->buffer_empty(get_area_rat, put_area_rat), actual_size == 0);

      EXPECT_EQ(m_buffer->get_data_size(get_type, put_area_rat), actual_size);
    }
    {
      evio::StreamBuf::GetThreadLock::crat get_area_rat(m_buffer->get_area_lock(get_type));
      EXPECT_EQ(m_buffer->buffer_empty(get_area_rat).is_transitory_true(), actual_size == 0);
      EXPECT_EQ(m_buffer->buffer_empty(get_area_rat).is_false(), actual_size != 0);             // If we are the GetThread then non-empty stays non-empty.

      // The allocated block has initially a size equal to the minimum block size.
      EXPECT_EQ(m_buffer->get_get_area_block_node(get_type)->get_size(), m_min_block_size);

      // The buffer is empty (and there is no race condition here, so the returned value is exact).
      //EXPECT_EQ(m_buffer->get_data_size_lower_bound(get_area_rat), (std::streamsize)actual_size);
    }
    {
      evio::StreamBuf::PutThreadLock::crat put_area_rat(m_buffer->put_area_lock(put_type));
      EXPECT_EQ(m_buffer->buffer_empty(put_area_rat).is_true(), actual_size == 0);              // If we are the PutThread then empty stays empty.
      EXPECT_EQ(m_buffer->buffer_empty(put_area_rat).is_transitory_false(), actual_size != 0);

      // The buffer is empty (and there is no race condition here, so the returned value is exact).
      EXPECT_EQ(m_buffer->get_data_size_upper_bound(put_area_rat), (std::streamsize)actual_size);
    }
  }
};

TEST_F(OutputBufferFixture, StreamBuf)
{
  // The buffer is empty.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 0UL);
  // Really empty.
  EXPECT_EQ(m_buffer->buf2dev_contiguous_forced(), 0UL);
  // But we have a get area.
  EXPECT_TRUE(m_buffer->buf2dev_ptr() != nullptr);
  // And a put area.
  EXPECT_TRUE(m_buffer->raw_pptr() != nullptr);

  // Test that the buffer uses our device.
  m_output_device->reset_sync_called();
  // Sanity check.
  EXPECT_FALSE(m_output_device->sync_called());
  // Trick to see which device this buffer uses: call sync(), this should call sync() on the device.
  m_buffer->sync();
  EXPECT_TRUE(m_output_device->sync_called());

  // The buffer only has a single allocated block.
  EXPECT_FALSE(m_buffer->has_multiple_blocks(get_type, put_type));
  // The start of the block equals the current get area.
  EXPECT_EQ(m_buffer->get_area_block_node_start(get_type), m_buffer->buf2dev_ptr());

  CALL(test_size(0));

  {
    evio::StreamBuf::GetThreadLock::crat get_area_rat(m_buffer->get_area_lock(get_type));
    // The get area is non-existent (we only have one block, and all of it is available for writing).
    EXPECT_EQ(m_buffer->unused_in_first_block(get_area_rat), 0UL);
  }
  {
    evio::StreamBuf::PutThreadLock::crat put_area_rat(m_buffer->put_area_lock(put_type));
    // The full block is available for writing.
    EXPECT_EQ(m_buffer->unused_in_last_block(put_area_rat), m_min_block_size);
  }
}

TEST_F(OutputBufferFixture, WriteData)
{
  // None of the classes derived from StreamBuf are allowed to add any members; only interface.
  // Therefore this cast should be entirely safe.
  evio::LinkBuffer* link_buffer = static_cast<evio::LinkBuffer*>(static_cast<evio::StreamBuf*>(m_buffer));

  // The room in the put area should be initially equal to the minimum_block_size.
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size);

  m_buffer->sputc('X');

  // The room in the put area is now one less.
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 1);

  // However, the get area was NOT updated.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 0UL);
  EXPECT_EQ(m_buffer->buf2dev_contiguous_forced(), 0UL);

  // The used size of the buffer is of course 1 byte.
  CALL(test_size(1));

  // Flush writes what is available in the get area to the linked device.
  m_buffer->flush();
  // But also does not update the get area.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 0UL);
  // Even though buf2dev_contiguous_forced() calls underflow_a(), which calls sync_next_egptr(),
  // m_next_egptr isn't updated until after sync_egptr() is called, which only happens after
  // calling our pbump (the one of StreamBuf) or when calling sync().
  EXPECT_EQ(m_buffer->buf2dev_contiguous_forced(), 0UL);

  // Only when explicitly flushing a *stream*, this happens.
  m_output << std::flush;
  // Now sync_egptr() was called, but sync_next_egptr() wasn't yet.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 0UL);
  // buf2dev_contiguous_forced() calls underflow_a(), which calls sync_next_egptr().
  EXPECT_EQ(m_buffer->buf2dev_contiguous_forced(), 1UL);

  // Writing a single character to the stream with flush.
  m_buffer->sputc('Y');
  // The room in the put area is now one less.
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 2);
  // As before.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 1UL);
  EXPECT_EQ(m_buffer->buf2dev_contiguous_forced(), 1UL);

  // The used size of the buffer is now 2 bytes.
  CALL(test_size(2));

  m_output << std::flush;
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 1UL);
  // Because there is still a single character available in the buffer; underflow_a and therefore sync_next_egptr wasn't called however.
  EXPECT_EQ(m_buffer->buf2dev_contiguous_forced(), 1UL);

  // Still 2...
  CALL(test_size(2));

  // After calling update_get_area directly...
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->update_get_area(m_buffer->get_get_area_block_node(get_type), cur_gptr, available, evio::StreamBuf::GetThreadLock::wat(m_buffer->get_area_lock(get_type)));
  }
  // The get area is updated.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 2UL);

  // Writing an endl is equivalent, of course.
  m_output << std::endl;
  CALL(test_size(3));
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 3);
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 2UL);
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->update_get_area(m_buffer->get_get_area_block_node(get_type), cur_gptr, available, evio::StreamBuf::GetThreadLock::wat(m_buffer->get_area_lock(get_type)));
  }
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 3UL);

  // Basically the same holds for writing multiple characters (provided it all fits in the put area).
  m_buffer->sputn("ABC", 3);
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 6);

  // The used size of the buffer is now 5 bytes.
  CALL(test_size(6));

  // Because sputn() calls (our) xsputn() (which calls xsputn_a) which uses our pbump(),
  // there is no need here to explicitly sync()/std::flush the buffer/stream, since our
  // pbump() already calls sync_egptr().
  // But the GetThread still needs to explicitly call sync_next_egptr() in order to
  // get the get area updated (this is omitted from buf2dev_contiguous() for optimization
  // reasons).
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 3UL);
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->update_get_area(m_buffer->get_get_area_block_node(get_type), cur_gptr, available, evio::StreamBuf::GetThreadLock::wat(m_buffer->get_area_lock(get_type)));
  }
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 6UL);

  // And therefore also when writing to an ostream in any way.
  m_output << "DEF";
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 9);

  // The used size of the buffer is now 9 bytes.
  CALL(test_size(9));

  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 6UL);
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->update_get_area(m_buffer->get_get_area_block_node(get_type), cur_gptr, available, evio::StreamBuf::GetThreadLock::wat(m_buffer->get_area_lock(get_type)));
  }
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 9UL);

  // Still 9...
  CALL(test_size(9));

  // If now we write 1 byte more than what fits in the remaining put area...
  std::vector<char> v(m_min_block_size, '!');
  m_output.write(&v[0], m_min_block_size - 8);

  CALL(test_size(m_min_block_size + 1));

  // Now we have two blocks.
  EXPECT_TRUE(m_buffer->has_multiple_blocks(get_type, put_type));
  // The room in the put area is again one less than its size (also this second block uses the minimum block size).
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 1);
  // And the get area still wasn't updated.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 9UL);
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->update_get_area(m_buffer->get_get_area_block_node(get_type), cur_gptr, available, evio::StreamBuf::GetThreadLock::wat(m_buffer->get_area_lock(get_type)));
  }
  // Available to read is now the whole first block, but not that extra byte of course.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), m_min_block_size);
  // And the room in the put area is one less than that.
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 1);

  // Fill up the put area precisely.
  m_output.write(&v[0], m_min_block_size - 1);
  CALL(test_size(m_min_block_size * 2));

  // The remaining room in the put area is now zero.
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), 0UL);

  // See StreamBuf::new_block_size.
  size_t new_block_size = utils::malloc_size(m_min_block_size * 2 + sizeof(evio::MemoryBlock)) - sizeof(evio::MemoryBlock);
  EXPECT_EQ(link_buffer->dev2buf_contiguous_forced(), new_block_size);
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), new_block_size);

  // The amount of data in the buffer didn't change.
  CALL(test_size(m_min_block_size * 2));

  std::string s;
  for (unsigned int i = 0; i < m_min_block_size * 2; ++i)
    s += m_buffer->sbumpc();
  EXPECT_EQ(s, "XY\nABCDEF!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
}

class MyInputDevice : public evio::InputDevice
{
 public:
  evio::InputBuffer* get_ibuffer() const { return m_ibuffer; }
  void set_dont_close() { m_flags |= INTERNAL_FDS_DONT_CLOSE; }
};

class MyInputDecoder : public evio::InputDecoder
{
 public:
  using evio::InputDecoder::InputDecoder;

  evio::RefCountReleaser decode(evio::MsgBlock&& msg, evio::GetThread type) override
  {
    DoutEntering(dc::notice, "MyInputDecoder::decode(\"" << libcwd::buf2str(msg.get_start(), msg.get_size()) << "\", type)");
    return {};
  }
};

class InputBufferFixture : public testing::Test
{
 protected:
  boost::intrusive_ptr<MyInputDevice> m_input_device;
  MyInputDecoder m_input;
  evio::InputBuffer* m_buffer;
  size_t m_min_block_size;

  evio::GetThread const get_type{};
  evio::PutThread const put_type{};

  InputBufferFixture() { }

  void SetUp()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v InputBufferFixture::SetUp()");
    debug::Mark setup;
#endif

    // Create a test InputDevice.
    m_input_device = evio::create<MyInputDevice>();
    m_input_device->init(0);           // Otherwise the device is not 'writable', which has influence on certain buffer functions (ie, sync()).
    m_input_device->set_dont_close();
    // Create an InputBuffer for it.
    m_input_device->input(m_input, 32);
    m_buffer = m_input_device->get_ibuffer();
    // Calculate the actual minimum block size, see StreamBuf::StreamBuf.
    m_min_block_size = utils::malloc_size(m_buffer->m_minimum_block_size + sizeof(evio::MemoryBlock)) - sizeof(evio::MemoryBlock);
  }

  void TearDown()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v InputBufferFixture::TearDown()");
    debug::Mark teardown;
#endif
    m_input_device.reset();
    // m_input can be reused (the call to input() above will effectively reset it).
  }
};

TEST_F(InputBufferFixture, ReadData)
{
  // Set up some test data.
  std::string const test_string = "Hello world!";

  // First fill the buffer with some data.
  m_buffer->sputn(test_string.data(), test_string.size());

  std::string read_back_buffer(test_string.size(), '!');
  // Read from the buffer until the end. Hence, precisely test_string.size() bytes will be read.
  EXPECT_EQ(m_buffer->sgetn(read_back_buffer.data(), 100), (std::streamsize)test_string.size());

  // Make sure we read the correct string ;).
  EXPECT_EQ(read_back_buffer, test_string);

  // Next write a block size of data.
  std::string test_string2 = std::string("abcdefghijklmnopqrstuvwxyz012345").substr(0, m_min_block_size - 1);
  if (m_min_block_size - 1 > test_string2.size())
    test_string2 += std::string(m_min_block_size - 1 - test_string2.size(), '*');
  test_string2 += '!';
  m_buffer->sputn(test_string2.data(), test_string2.size());

  // This should perfectly fit in the first block, because the block was empty after reading it back,
  // which should have reset the put area to the beginning of the block at the moment we started
  // writing again.
  EXPECT_FALSE(m_buffer->has_multiple_blocks(get_type, put_type));

  // Read everything character by character.
  std::string s;
  for (unsigned int i = 0; i < m_min_block_size; ++i)
    s += m_buffer->sbumpc();
  EXPECT_EQ(s, test_string2.c_str());

  // The buffer is now entirely empty.
  {
    evio::StreamBuf::GetThreadLock::crat get_area_rat(m_buffer->get_area_lock(get_type));
    evio::StreamBuf::PutThreadLock::crat put_area_rat(m_buffer->put_area_lock(put_type));
    EXPECT_TRUE(m_buffer->buffer_empty(get_area_rat, put_area_rat));
    EXPECT_EQ(m_buffer->unused_in_first_block(get_area_rat), m_min_block_size);
    EXPECT_EQ(m_buffer->unused_in_last_block(put_area_rat), 0UL);
  }
  EXPECT_FALSE(m_buffer->has_multiple_blocks(get_type, put_type));

  // But when we write to it - we DO end up with a new memory block, because last_gptr was not updated.
  EXPECT_EQ(m_buffer->sputn(".", 1), 1);
  EXPECT_TRUE(m_buffer->has_multiple_blocks(get_type, put_type));
}

class AInputStream : public std::istream
{
};

class AOutputStream : public std::ostream
{
};

class LinkBufferFixture : public testing::Test
{
 protected:
  boost::intrusive_ptr<MyInputDevice> m_input_device;
  boost::intrusive_ptr<MyOutputDevice> m_output_device;
  evio::LinkBuffer* m_buffer;
  size_t m_min_block_size;
  AInputStream m_input;
  AOutputStream m_output;

  evio::GetThread const get_type{};
  evio::PutThread const put_type{};

  LinkBufferFixture() { }

  void SetUp()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v LinkBufferFixture::SetUp()");
    debug::Mark setup;
#endif

    // Create a test InputDevice.
    m_input_device = evio::create<MyInputDevice>();
    m_input_device->init(0);           // Otherwise the device is not 'writable', which has influence on certain buffer functions (ie, sync()).
    m_input_device->set_dont_close();
    // Create a test OutputDevice.
    m_output_device = evio::create<MyOutputDevice>();
    m_output_device->init(1);           // Otherwise the device is not 'writable', which has influence on certain buffer functions (ie, sync()).
    m_output_device->set_dont_close();
    // Create a LinkBuffer for it.
    m_output_device->output(m_input_device, 32, 256, -1);
    m_buffer = static_cast<evio::LinkBuffer*>(static_cast<evio::Dev2Buf*>(m_input_device->get_ibuffer()));
    // Calculate the actual minimum block size, see StreamBuf::StreamBuf.
    m_min_block_size = utils::malloc_size(m_buffer->m_minimum_block_size + sizeof(evio::MemoryBlock)) - sizeof(evio::MemoryBlock);
    // Use the link buffer for our ostream object.
    m_output.rdbuf(m_buffer->rdbuf());
    // And also for our istream object.
    m_input.rdbuf(m_buffer->rdbuf());
  }

  void TearDown()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v LinkBufferFixture::TearDown()");
    debug::Mark teardown;
#endif
    m_input_device.reset();
    m_output_device.reset();
    // m_input can be reused (the call to input() above will effectively reset it).
  }
};

TEST_F(LinkBufferFixture, Streaming)
{
  auto& size_hash_pair(utils::HasherStreamBuf::size_hash_pairs[utils::HasherStreamBuf::size_hash_pairs.size() - 1]);

  utils::RandomStreamBuf random(size_hash_pair.size, 'A', 'Z');
  utils::StreamHasher hasher;

  std::vector<char> buf(m_buffer->m_minimum_block_size + 3);
  std::array<size_t, 7> const test_sizes = {
    1, 2, 5, m_buffer->m_minimum_block_size - 3, m_buffer->m_minimum_block_size - 1, m_buffer->m_minimum_block_size, m_buffer->m_minimum_block_size + 1
  };
  std::default_random_engine random_engine;
  std::uniform_int_distribution<int> dist(0, test_sizes.size() - 1);

  std::streamsize size, len;

  Debug(dc::evio.off());
  Dout(dc::notice, "Writing " << size_hash_pair.size << " bytes to the buffer.");
  do
  {
    size = test_sizes[dist(random_engine)];
    len = random.sgetn(buf.data(), size);
    m_output.write(buf.data(), len);
  }
  while (len == size);

  Dout(dc::notice, "Reading " << size_hash_pair.size << " bytes from the buffer.");
  do
  {
    size = test_sizes[dist(random_engine)];
    len = m_buffer->sgetn(buf.data(), size);
    hasher.write(buf.data(), len);
  }
  while (len == size);
  Debug(dc::evio.on());

  EXPECT_EQ(hasher.digest(), size_hash_pair.hash);
}
