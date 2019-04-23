#include "sys.h"
#include "debug.h"
#include "evio/StreamBuf.h"
#include <cstring>
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

size_t constexpr minimum_blocksize = 64;
size_t constexpr max_alloc = 1024 * minimum_blocksize;
size_t constexpr buffer_full_watermark = max_alloc * 0.8;

// Buffer used when reading input from a device (fd).
// For this test case we read from a C array.
class MyInputBuffer : public evio::InputBuffer
{
  public:
   using InputBuffer::InputBuffer;
   void write(char const* device, size_t len);
   void read(char* buf, size_t len);
};

// Read len bytes from device and write into InputBuffer.
void MyInputBuffer::write(char const* device, size_t len)
{
  while (len > 0)
  {
    size_t wlen = std::min(len, dev2buf_contiguous());
    if (wlen == 0)
    {
      wlen = std::min(len, dev2buf_contiguous_forced());
      ASSERT(wlen > 0);
    }
    char* put_area = dev2buf_ptr();             // Use dev2buf, as data is moving from the device to the buffer.
    std::memcpy(put_area, device, wlen);        // Use memcpy for this test, instead of actually reading from a device fd.
    dev2buf_bump(wlen);
    len -= wlen;
    device += wlen;
  }
}

// Read len bytes from InputBuffer and write to buf.
void MyInputBuffer::read(char* buf, size_t len)
{
  evio::GetThread type;
  while (len > 0)
  {
    size_t rlen = std::min(len, force_next_contiguous_number_of_bytes(type));       // Use force_next_contiguous_number_of_bytes() before writing to raw_gptr()!
    ASSERT(rlen > 0);
    {
      StreamBuf::GetThreadLock::wat get_area_wat(get_area_lock(type));
      std::memcpy(buf, raw_gptr(get_area_wat), rlen);         // We read from the get area.
      raw_gbump(get_area_wat, rlen);
    }
    buf += rlen;
    len -= rlen;
  }
}

// Buffer used when writing output to a device (fd).
// For this test case we write to a C array.
class MyOutputBuffer : public evio::OutputBuffer
{
  public:
   using OutputBuffer::OutputBuffer;
   void write(char const* buf, size_t len);
   void read(char* device, size_t len);
};

// Write len bytes from buf into OutputBuffer.
void MyOutputBuffer::write(char const* buf, size_t len)
{
  evio::PutThread type;
  while (len > 0)
  {
    size_t wlen = std::min(len, available_contiguous_number_of_bytes(PutThreadLock::rat(put_area_lock(type))));
    if (wlen == 0)
    {
      wlen = std::min(len, force_available_contiguous_number_of_bytes(type));
      ASSERT(wlen > 0);
    }
    char* put_area = raw_pptr();                // We write to the put area.
    std::memcpy(put_area, buf, wlen);
    raw_pbump(wlen);
    len -= wlen;
    buf += wlen;
  }
}

// Read len bytes from OutputBuffer and write to a device (fd).
// For this test we use a C array as device.
void MyOutputBuffer::read(char* device, size_t len)
{
  while (len > 0)
  {
    size_t rlen = std::min(len, buf2dev_contiguous());
    if (rlen == 0)
    {
      rlen = std::min(len, buf2dev_contiguous_forced());
      ASSERT(rlen > 0);
    }
    std::memcpy(device, buf2dev_ptr(), rlen);   // Use memcpy instead of actually writing to a fd.
                                                // Use buf2dev because data is moving from the buffer to the device.
    buf2dev_bump(rlen);
    device += rlen;
    len -= rlen;
  }
}

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  using namespace evio;

  //===========================================================================
  // Test InputBuffer.
  //

  if (1)
  {
    MyInputBuffer* sb = new MyInputBuffer(nullptr, minimum_blocksize, max_alloc, buffer_full_watermark);

    size_t const tlen = 2000;
    char* const buf1 = new char [tlen];
    for (size_t i = 0; i < tlen; ++i)
      buf1[i] = 'A' + (i % 25);
    char* const buf2 = new char [tlen];
    std::memset(buf2, 0, tlen);

    char const* device = buf1;
    char* rbuf = buf2;
    size_t size = 0;
    size_t len = tlen;
    while (len > 0)
    {
      // Write 25 chars at a time.
      size_t wlen = std::min(len, (size_t)25);
      sb->write(device, wlen);
      Debug(sb->printOn(std::cout));
      device += wlen;
      len -= wlen;
      size += wlen;

      // While reading 10 chars at a time.
      size_t rlen = std::min(size, (size_t)10);
      sb->read(rbuf, rlen);
      size -= rlen;
      rbuf += rlen;
    }
    sb->read(rbuf, size);
    ASSERT(std::memcmp(buf1, buf2, 2000) == 0);

    // Again using raw_sgetn.
    std::memset(buf2, 0, tlen);
    device = buf1;
    rbuf = buf2;
    size = 0;
    len = tlen;
    while (len > 0)
    {
      // Write 25 chars at a time.
      size_t wlen = std::min(len, (size_t)25);
      sb->write(device, wlen);
      Debug(sb->printOn(std::cout));
      device += wlen;
      len -= wlen;
      size += wlen;

      // While reading 10 chars at a time.
      size_t rlen = std::min(size, (size_t)10);
      sb->raw_sgetn(rbuf, rlen);
      size -= rlen;
      rbuf += rlen;
    }
    sb->read(rbuf, size);
    ASSERT(std::memcmp(buf1, buf2, 2000) == 0);
  }

  //===========================================================================
  // Test OutputBuffer.
  //

  if (1)
  {
    MyOutputBuffer* sb = new MyOutputBuffer(nullptr, minimum_blocksize, max_alloc, buffer_full_watermark);

    evio::GetThread get_thread;
    evio::PutThread put_thread;

    Dout(dc::notice, "m_minimum_block_size = " << sb->m_minimum_block_size);
    Dout(dc::notice, "has_multiple_blocks() = " << sb->has_multiple_blocks(get_thread, put_thread));
    Dout(dc::notice, "unused_in_first_block() = " << sb->unused_in_first_block(evio::StreamBuf::GetThreadLock::rat(sb->get_area_lock(get_thread))));
    Dout(dc::notice, "unused_in_last_block() = " << sb->unused_in_last_block(evio::StreamBuf::PutThreadLock::rat(sb->put_area_lock(put_thread))));
    Dout(dc::notice, "get_data_size() = " << sb->get_data_size(get_thread, evio::StreamBuf::PutThreadLock::rat(sb->put_area_lock(put_thread))));
    Dout(dc::notice, "m_max_allocated_block_size = " << sb->m_max_allocated_block_size);
    // Protected.
    //Dout(dc::notice, "next_contiguous_number_of_bytes() = " << sb->next_contiguous_number_of_bytes());
    size_t len = 0;
    while (sb->is_contiguous(len, evio::StreamBuf::GetThreadLock::rat(sb->get_area_lock(get_thread))))
      ++len;
    --len;
    Dout(dc::notice, "is_contiguous(" << len << ") is largest size that return true.");
    // new_block_size is private because it calls get_data_size_upper_bound() which is fuzzy.
    //Dout(dc::notice, "new_block_size() = " << sb->new_block_size(put_thread));
//    Dout(dc::notice, "buffer_full() = " << (sb->buffer_full() ? "true" : "false"));
    bool is_empty;
    {
      evio::StreamBuf::GetThreadLock::rat get_area_rat(sb->get_area_lock(get_thread));
      evio::StreamBuf::PutThreadLock::rat put_area_rat(sb->put_area_lock(put_thread));
      is_empty = sb->buffer_empty(get_area_rat, put_area_rat);
      Dout(dc::notice, "buffer_empty() = " << (is_empty ? "true" : "false"));
    }

    Dout(dc::notice, "Empty buffer:");
    ASSERT(is_empty);
    Debug(sb->printOn(std::cout));

//    ASSERT(!sb->buffer_full());
    ASSERT(len == sb->unused_in_last_block(evio::StreamBuf::PutThreadLock::rat(sb->put_area_lock(put_thread))));
    ASSERT(len > 0);

    size_t const tlen = 2000;
    char* const buf1 = new char [tlen];
    for (size_t i = 0; i < tlen; ++i)
      buf1[i] = 'A' + (i % 25);
    char* const buf2 = new char [tlen];
    std::memset(buf2, 0, tlen);

    char const* buf = buf1;
    char* device = buf2;
    size_t size = 0;
    len = tlen;
    while (len > 0)
    {
      // Write 25 chars at a time.
      size_t wlen = std::min(len, (size_t)25);
      sb->write(buf, wlen);
      Debug(sb->printOn(std::cout));
      buf += wlen;
      len -= wlen;
      size += wlen;

      // While reading 10 chars at a time.
      size_t rlen = std::min(size, (size_t)10);
      sb->read(device, rlen);
      size -= rlen;
      device += rlen;
    }
    sb->read(device, size);
    ASSERT(std::memcmp(buf1, buf2, 2000) == 0);

    // Again using raw_sputn.
    std::memset(buf2, 0, tlen);
    buf = buf1;
    device = buf2;
    size = 0;
    len = tlen;
    while (len > 0)
    {
      // Write 25 chars at a time.
      size_t wlen = std::min(len, (size_t)25);
      sb->raw_sputn(buf, wlen);
      Debug(sb->printOn(std::cout));
      buf += wlen;
      len -= wlen;
      size += wlen;

      // While reading 10 chars at a time.
      size_t rlen = std::min(size, (size_t)10);
      sb->read(device, rlen);
      size -= rlen;
      device += rlen;
    }
    sb->read(device, size);
    ASSERT(std::memcmp(buf1, buf2, 2000) == 0);
  }
}
