#include "evio/InputDecoder.h"
#include "evio/StreamBuf.h"
#include "utils/RandomStream.h"
#include "utils/StreamHasher.h"
#include <fcntl.h>
#include <unistd.h>
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif
#include "NoEpollDevices.h"

using evio::RefCountReleaser;

TEST(StreamBuf, TypeDefs) {

  // Typedefs.
  using StreamBuf = evio::StreamBuf;
  EXPECT_TRUE((std::is_same<StreamBuf::char_type, char>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::traits_type, std::char_traits<char>>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::int_type, int>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::pos_type, std::streampos>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::off_type, std::streamoff>::value));
}

class StreamBuf_OutputDevice : public NoEpollOutputDevice
{
 private:
  bool m_sync_called;

 public:
  evio::OutputBuffer* get_obuffer() const { return m_obuffer; }

  NAD_DECL_PUBLIC(stop_output_device)
  {
    NAD_PUBLIC_BEGIN;
    NAD_CALL_FROM_PUBLIC(evio::OutputDevice::stop_output_device);
    NAD_PUBLIC_END;
  }

  void init(int fd)
  {
    evio::OutputDevice::init(fd);
  }

 protected:
  int sync() override { m_sync_called = true; return evio::OutputDevice::sync(); }

 public:
  void reset_sync_called() { m_sync_called = false; }
  bool sync_called() const { return m_sync_called; }
  void set_dont_close() { state_t::wat(m_state)->m_flags.set_dont_close(); }
};

#include "EventLoopFixture.h"

class OutputBufferFixture : public EventLoopFixture<testing::Test>
{
 protected:
  boost::intrusive_ptr<StreamBuf_OutputDevice> m_output_device;
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

    EventLoopFixture<testing::Test>::SetUp();
    // Create a test OutputDevice.
    m_output_device = evio::create<StreamBuf_OutputDevice>();
    m_output_device->init(1);           // Otherwise the device is not 'writable', which has influence on certain buffer functions (ie, sync()).
    m_output_device->set_dont_close();
    // Create an OutputBuffer for it.
    m_output_device->set_source(m_output, 32);
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

    force_exit();
    m_output_device.reset();
    // m_output can be reused (the call to set_source() above will effectively reset it).
    EventLoopFixture<testing::Test>::TearDown();
  }

  void test_size(size_t actual_size)
  {
    EXPECT_EQ(m_buffer->buffer_empty(), actual_size == 0);
    EXPECT_EQ(m_buffer->get_data_size(), actual_size);

    EXPECT_EQ(m_buffer->StreamBufConsumer::buffer_empty().is_transitory_true(), actual_size == 0);
    EXPECT_EQ(m_buffer->StreamBufConsumer::buffer_empty().is_false(), actual_size != 0);                // If we are the consumer thread then non-empty stays non-empty.

    // The allocated block has initially a size equal to the minimum block size.
    EXPECT_EQ(m_buffer->get_get_area_block_node()->get_size(), m_min_block_size);

    // The buffer is empty (and there is no race condition here, so the returned value is exact).
    //EXPECT_EQ(m_buffer->get_data_size_lower_bound(get_area_rat), (std::streamsize)actual_size);

    EXPECT_EQ(m_buffer->StreamBufProducer::buffer_empty().is_true(), actual_size == 0);              // If we are the PutThread then empty stays empty.
    EXPECT_EQ(m_buffer->StreamBufProducer::buffer_empty().is_transitory_false(), actual_size != 0);

    // The buffer is empty (and there is no race condition here, so the returned value is exact).
    EXPECT_EQ(m_buffer->get_data_size_upper_bound(), (std::streamsize)actual_size);
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
  // Trick to see which device this buffer uses: call pubsync(), this should call sync() on the device.
  m_buffer->pubsync();
  EXPECT_TRUE(m_output_device->sync_called());

  // The buffer only has a single allocated block.
  EXPECT_FALSE(m_buffer->has_multiple_blocks());
  // The start of the block equals the current get area.
  EXPECT_EQ(m_buffer->get_area_block_node_start(), m_buffer->buf2dev_ptr());

  CALL(test_size(0));

  // The get area is non-existent (we only have one block, and all of it is available for writing).
  EXPECT_EQ(m_buffer->unused_in_first_block(), 0UL);

  // The full block is available for writing.
  EXPECT_EQ(m_buffer->unused_in_last_block(), m_min_block_size);
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

#if defined(CWDEBUG) || defined(DEBUG)
  // After calling update_get_area directly...
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->debug_update_get_area(m_buffer->get_get_area_block_node(), cur_gptr, available);
  }
  // The get area is updated.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 2UL);
#endif

  // Writing an endl is equivalent, of course.
  m_output << std::endl;
  CALL(test_size(3));
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 3);
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 2UL);
#if defined(CWDEBUG) || defined(DEBUG)
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->debug_update_get_area(m_buffer->get_get_area_block_node(), cur_gptr, available);
  }
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 3UL);
#endif

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
#if defined(CWDEBUG) || defined(DEBUG)
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->debug_update_get_area(m_buffer->get_get_area_block_node(), cur_gptr, available);
  }
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 6UL);
#endif

  // And therefore also when writing to an ostream in any way.
  m_output << "DEF";
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 9);

  // The used size of the buffer is now 9 bytes.
  CALL(test_size(9));

  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 6UL);
#if defined(CWDEBUG) || defined(DEBUG)
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->debug_update_get_area(m_buffer->get_get_area_block_node(), cur_gptr, available);
  }
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 9UL);
#endif

  // Still 9...
  CALL(test_size(9));

  // If now we write 1 byte more than what fits in the remaining put area...
  std::vector<char> v(m_min_block_size, '!');
  m_output.write(&v[0], m_min_block_size - 8);

  CALL(test_size(m_min_block_size + 1));

  // Now we have two blocks.
  EXPECT_TRUE(m_buffer->has_multiple_blocks());
  // The room in the put area is again one less than its size (also this second block uses the minimum block size).
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 1);
  // And the get area still wasn't updated.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), 9UL);
#if defined(CWDEBUG) || defined(DEBUG)
  {
    char* cur_gptr;
    std::streamsize available;
    m_buffer->debug_update_get_area(m_buffer->get_get_area_block_node(), cur_gptr, available);
  }
  // Available to read is now the whole first block, but not that extra byte of course.
  EXPECT_EQ(m_buffer->buf2dev_contiguous(), m_min_block_size);
  // And the room in the put area is one less than that.
  EXPECT_EQ(link_buffer->dev2buf_contiguous(), m_min_block_size - 1);
#endif

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

class StreamBuf_InputDevice : public NoEpollInputDevice
{
 public:
  evio::InputBuffer* get_ibuffer() const { return m_ibuffer; }
  void set_dont_close() { state_t::wat(m_state)->m_flags.set_dont_close(); }
  void init(int fd) { evio::InputDevice::init(fd); }
};

class StreamBuf_InputDecoder : public evio::InputDecoder
{
 public:
  using evio::InputDecoder::InputDecoder;

  NAD_DECL_CWDEBUG_ONLY(decode, evio::MsgBlock&& CWDEBUG_ONLY(msg)) override
  {
    DoutEntering(dc::notice, "StreamBuf_InputDecoder::decode(" << NAD_DoutEntering_ARG0 << "\"" << libcwd::buf2str(msg.get_start(), msg.get_size()) << "\")");
  }
};

class InputBufferFixture : public testing::Test
{
 protected:
  boost::intrusive_ptr<StreamBuf_InputDevice> m_input_device;
  StreamBuf_InputDecoder m_input;
  evio::InputBuffer* m_buffer;
  size_t m_min_block_size;
  int m_pipefd[2];

  evio::GetThread const get_type{};
  evio::PutThread const put_type{};

  InputBufferFixture() { }

  void SetUp()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v InputBufferFixture::SetUp()");
    debug::Mark setup;
#endif

    // Get some valid fd.
    if (pipe2(m_pipefd, O_CLOEXEC) == -1)
      DoutFatal(dc::core|error_cf, "pipe2(" << m_pipefd << ", O_CLOEXEC) = -1");

    // Create a test InputDevice.
    m_input_device = evio::create<StreamBuf_InputDevice>();
    m_input_device->init(m_pipefd[1]);           // Otherwise the device is not 'writable', which has influence on certain buffer functions (ie, sync()).
    // Create an InputBuffer for it.
    m_input_device->set_sink(m_input, 32);
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
    // m_input can be reused (the call to set_sink() above will effectively reset it).

    close(m_pipefd[0]);
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
  EXPECT_FALSE(m_buffer->has_multiple_blocks());

  // Read everything character by character.
  std::string s;
  for (unsigned int i = 0; i < m_min_block_size; ++i)
    s += m_buffer->sbumpc();
  EXPECT_EQ(s, test_string2.c_str());

  // The buffer is now entirely empty.
  {
    EXPECT_TRUE(m_buffer->buffer_empty());
    EXPECT_EQ(m_buffer->unused_in_first_block(), m_min_block_size);
    EXPECT_EQ(m_buffer->unused_in_last_block(), 0UL);
  }
  EXPECT_FALSE(m_buffer->has_multiple_blocks());

  // But when we write to it - we DO end up with a new memory block, because last_gptr was not updated.
  EXPECT_EQ(m_buffer->sputn(".", 1), 1);
  EXPECT_TRUE(m_buffer->has_multiple_blocks());
}

class AInputStream : public std::istream
{
};

class AOutputStream : public std::ostream
{
};

class LinkBufferFixture : public EventLoopFixture<testing::Test>
{
 protected:
  boost::intrusive_ptr<StreamBuf_InputDevice> m_input_device;
  boost::intrusive_ptr<StreamBuf_OutputDevice> m_output_device;
  evio::LinkBuffer* m_buffer;
  size_t m_min_block_size;
  AInputStream m_input;
  AOutputStream m_output;
  int m_pipefd[2];
  static constexpr size_t minimum_block_size = 32;

  evio::GetThread const get_type{};
  evio::PutThread const put_type{};

  LinkBufferFixture() { }

  void SetUp()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v LinkBufferFixture::SetUp()");
    debug::Mark setup;
#endif
    EventLoopFixture<testing::Test>::SetUp();

    // Get some valid fd.
    if (pipe2(m_pipefd, O_CLOEXEC) == -1)
      DoutFatal(dc::core|error_cf, "pipe2(" << m_pipefd << ", O_CLOEXEC) = -1");

    // Create a test InputDevice.
    m_input_device = evio::create<StreamBuf_InputDevice>();
    m_input_device->init(m_pipefd[1]);  // Otherwise the device is not 'writable', which has influence on certain buffer functions (ie, sync()).
    m_input_device->set_dont_close();
    // Create a test OutputDevice.
    m_output_device = evio::create<StreamBuf_OutputDevice>();
    m_output_device->init(m_pipefd[0]); // Otherwise the device is not 'writable', which has influence on certain buffer functions (ie, sync()).
    m_output_device->set_dont_close();
    // Create a LinkBuffer for it.
    m_output_device->set_source(m_input_device, minimum_block_size, 8 * minimum_block_size, -1);
    m_buffer = static_cast<evio::LinkBuffer*>(static_cast<evio::Dev2Buf*>(m_input_device->get_ibuffer()));
    // Calculate the actual minimum block size, see StreamBuf::StreamBuf.
    m_min_block_size = utils::malloc_size(m_buffer->m_minimum_block_size + sizeof(evio::MemoryBlock)) - sizeof(evio::MemoryBlock);
    ASSERT(m_min_block_size == minimum_block_size);
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
    force_exit();
    m_input_device.reset();
    m_output_device.reset();
    // m_input can be reused (the call to set_sink() above will effectively reset it).
    EventLoopFixture<testing::Test>::TearDown();
  }
};

class RandomFixture : public LinkBufferFixture
{
 protected:
  utils::HasherStreamBuf::size_hash_pair_t const size_hash_pair;
  utils::RandomStreamBuf random;
  utils::StreamHasher hasher;
  std::vector<char> buf;
  std::array<size_t, 8> const test_sizes = {
    1, 2, 5, minimum_block_size - 3, minimum_block_size - 1, minimum_block_size, minimum_block_size + 1, minimum_block_size + 2
  };
  std::default_random_engine random_engine;
  std::uniform_int_distribution<int> read_dist;
  std::uniform_int_distribution<int> write_dist;
  std::uniform_int_distribution<int> true_or_false;
  bool use_fill;
  std::atomic_bool done;

  RandomFixture() :
    size_hash_pair(utils::HasherStreamBuf::size_hash_pairs[8]), // 10^8 bytes.
    random(size_hash_pair.size, 'A', 'Z'),
    buf(minimum_block_size + 3),
    random_engine(160433238),
    read_dist(0, test_sizes.size() - 1),
    write_dist(0, test_sizes.size() - 2),
    true_or_false(0, 1),
    use_fill(false),
    done(false) { }

  std::streamsize fill(std::streamsize offset, char* buf, std::streamsize len);

  void SetUp()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v RandomFixture::SetUp()");
    debug::Mark setup;
#endif
    LinkBufferFixture::SetUp();
    if (size_hash_pair.size > 1000)
      Debug(dc::evio.off());
#ifdef DEBUGSTREAMBUFSTATS
    m_buffer->StreamBufProducer::reset_stats();
    m_buffer->StreamBufConsumer::reset_stats();
#endif
  }

  void TearDown()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v RandomFixture::TearDown()");
    debug::Mark teardown;
#endif
#ifdef DEBUGSTREAMBUFSTATS
    m_buffer->StreamBufProducer::dump_stats();
    m_buffer->StreamBufConsumer::dump_stats();
#endif
    if (size_hash_pair.size > 1000)
      Debug(dc::evio.on());
    size_t digest = size_hash_pair.hash;
    if (use_fill)
      switch (size_hash_pair.size)
      {
        case 1:
          digest = 1718778494716316058UL;
          break;
        case 10:
          digest = 7103722163251064523UL;
          break;
        case 100:
          digest = 3805333619560949641UL;
          break;
        case 1000:
          digest = 8152631729821103751UL;
          break;
        case 10000:
          digest = 13553392714589110238UL;
          break;
        case 100000:
          digest = 6873471487155913012UL;
          break;
        case 1000000:
          digest = 4029632496973681816UL;
          break;
        case 10000000:
          digest = 7125079558958757456UL;
          break;
        case 100000000:
          digest = 31304502237803872UL;
          break;

      }
    EXPECT_EQ(hasher.digest(), digest);
    LinkBufferFixture::TearDown();
  }

 public:
  void write_thread(int test);
  void read_thread(int test);
};

TEST_F(RandomFixture, Streaming_WriteThenRead)
{
  Dout(dc::notice, "Writing " << size_hash_pair.size << " bytes to the buffer.");
  std::streamsize size, len;
  do
  {
    size = test_sizes[write_dist(random_engine)];
    len = random.sgetn(buf.data(), size);
    m_output.write(buf.data(), len);
  }
  while (len == size);

  Dout(dc::notice, "Reading " << size_hash_pair.size << " bytes from the buffer.");
  do
  {
    size = test_sizes[read_dist(random_engine)];
    len = m_buffer->sgetn(buf.data(), size);
    hasher.write(buf.data(), len);
  }
  while (len == size);
}

TEST_F(RandomFixture, Streaming_AlternateWriteRead)
{
  Dout(dc::notice, "Writing/Reading " << size_hash_pair.size << " bytes to/from the buffer.");
  bool writing_finished = false;
  bool reading_finished = false;
  do
  {
    if (!writing_finished)
    {
      std::streamsize size = test_sizes[write_dist(random_engine)];
      std::streamsize len = random.sgetn(buf.data(), size);
      m_output.write(buf.data(), len);
      writing_finished = len != size;
      if (writing_finished)
        m_output << std::flush;
    }
    std::streamsize size = test_sizes[read_dist(random_engine)];
    std::streamsize len = m_buffer->sgetn(buf.data(), size);
    reading_finished = writing_finished && len != size;
    hasher.write(buf.data(), len);
  }
  while (!reading_finished);
}

std::streamsize RandomFixture::fill(std::streamsize offset, char* buf, std::streamsize len)
{
  static char data[129] = "abcdefghijklmnopqrstuvwxyz789012ABCDEFGHIJKLMNOPQRSTUVWXYZ&*()!\nabcdefghijklmnopqrstuvwxyz789012ABCDEFGHIJKLMNOPQRSTUVWXYZ&*()!\n";
  ASSERT(len < 64);
  std::memcpy(buf, data + offset % 64, len);
  return std::min(len, std::streamsize(size_hash_pair.size) - offset);
}

void RandomFixture::write_thread(int test)
{
  Debug(NAMESPACE_DEBUG::init_thread("PutThread", copy_from_main));
  DoutEntering(dc::notice, "RandomFixture::write_thread(" << test << ")");
  bool writing_finished = false;
  std::default_random_engine write_thread_random_engine;
  std::ios::iostate old_exceptions = m_output.exceptions();
  m_output.exceptions(std::ios::eofbit | std::ios::failbit | std::ios::badbit);
  ASSERT(m_output.good());
  try
  {
    size_t total_size = 0;
#ifdef DEBUGEVENTRECORDING
    m_buffer->write_stream_offset = 0;
#endif
    do
    {
      std::streamsize size = test_sizes[write_dist(write_thread_random_engine)];
#ifdef DEBUGEVENTRECORDING
      std::streamsize len = fill(total_size, buf.data(), size);
      ASSERT(m_buffer->write_stream_offset == total_size);
#else
      std::streamsize len = random.sgetn(buf.data(), size);
#endif
      if (test == 1 && true_or_false(write_thread_random_engine) == 1)
      {
        for (int i = 0; i < len; ++i)
          m_buffer->sputc(buf[i]);
#ifdef DEBUGEVENTRECORDING
        m_buffer->write_stream_offset = total_size + len;
#endif
        Dout(dc::evio, "Wrote \"" << libcwd::buf2str(&buf[0], len) << "\" using sputc.");
      }
      else
        m_output.write(buf.data(), len);
      total_size += len;
      Dout(dc::evio, "Wrote " << total_size << " bytes so far.");
      writing_finished = len != size;
      if (writing_finished)
        m_output << std::flush;
    }
    while (!writing_finished);
    Dout(dc::notice|flush_cf, "Wrote " << total_size << " bytes.");
  }
  catch (std::ios_base::failure const& error)
  {
    DoutFatal(dc::core, "Caught exception " << error.what());
  }
  m_output.exceptions(old_exceptions);
  done = true;
}

void RandomFixture::read_thread(int test)
{
  Debug(NAMESPACE_DEBUG::init_thread("GetThread", copy_from_main));
  DoutEntering(dc::notice, "RandomFixture::read_thread(" << test << ")");
#ifdef CWDEBUG
  ASSERT(m_buffer->get_get_area_block_node());
#endif
  std::default_random_engine read_thread_random_engine;
  std::vector<char> read_thread_buf(minimum_block_size + 3);
  size_t total_size = 0;
#ifdef DEBUGEVENTRECORDING
  m_buffer->read_stream_offset = 0;
  char compare_buf[128];
#endif
  do
  {
    std::streamsize size = test_sizes[read_dist(read_thread_random_engine)];
    std::streamsize len;
    size_t count = 0;
    do
    {
#ifdef DEBUGEVENTRECORDING
      ASSERT(m_buffer->read_stream_offset == total_size);
#endif
      if (test == 1 && true_or_false(read_thread_random_engine) == 1)
      {
        len = m_buffer->buf2dev_contiguous();
        if (len == 0)
          len = m_buffer->buf2dev_contiguous_forced();
        len = std::min(len, size);
        for (int i = 0; i < len; ++i)
          read_thread_buf[i] = m_buffer->sbumpc();
#ifdef DEBUGEVENTRECORDING
        m_buffer->read_stream_offset = total_size + len;
#endif
      }
      else
        len = m_buffer->sgetn(read_thread_buf.data(), size);
      Dout(dc::evio, "Read: \"" << libcwd::buf2str(read_thread_buf.data(), len) << "\".");
      if (len == 0)
      {
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        if (++count > 1000)
        {
#ifdef DEBUGEVENTRECORDING
          for (auto data : m_buffer->recording_buffer)
          {
            Dout(dc::notice, *data);
          }
          DoutFatal(dc::core, "Read " << total_size << " bytes.");
#endif
          ASSERT(!done);
          count = 0;
        }
      }
      else
        count = 0;
    }
    while (len == 0);
#ifdef DEBUGEVENTRECORDING
    std::streamsize ss = fill(total_size, compare_buf, len);
    ASSERT(ss == len);
    if (strncmp(compare_buf, read_thread_buf.data(), len))
    {
      Dout(dc::notice, "Read \"" << libcwd::buf2str(read_thread_buf.data(), len) << "\", expected \"" << libcwd::buf2str(compare_buf, len) << "\".");
#ifdef DEBUGKEEPMEMORYBLOCKS
      m_buffer->dump();
#endif
      for (auto data : m_buffer->recording_buffer)
      {
        Dout(dc::notice, *data);
      }
      DoutFatal(dc::core, "Read \"" << libcwd::buf2str(read_thread_buf.data(), len) << "\", expected \"" << libcwd::buf2str(compare_buf, len) << "\".");
    }
#endif
    total_size += len;
    hasher.write(read_thread_buf.data(), len);
  }
  while (total_size < size_hash_pair.size);
}

TEST_F(RandomFixture, Streaming_ConcurrentWriteRead)
{
  Dout(dc::notice, "Writing/Reading " << size_hash_pair.size << " bytes to/from the buffer.");
#ifdef DEBUGEVENTRECORDING
  use_fill = true;
#endif
  std::thread t1(&RandomFixture::read_thread, this, 0);
  std::thread t2(&RandomFixture::write_thread, this, 0);
  t1.join();
  t2.join();
}

TEST_F(RandomFixture, Streaming_ConcurrentSputcSbumpc)
{
  Dout(dc::notice, "Writing/Reading " << size_hash_pair.size << " bytes to/from the buffer.");
#ifdef DEBUGEVENTRECORDING
  use_fill = true;
#endif
  std::thread t1(&RandomFixture::read_thread, this, 1);
  std::thread t2(&RandomFixture::write_thread, this, 1);
  t1.join();
  t2.join();
}
