// Install https://github.com/MPI-SWS/genmc
//
// Then test with:
//
// genmc -unroll=3 -- -I$REPOBASE-objdir/src genmc_buffer_reset_test.c

// These header files are replaced by genmc (see /usr/local/include/genmc):
#include <pthread.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <stdatomic.h>
//#include <stdio.h>

char buffer[4] = { 0, 0, 0, 0 };
char* const block_start = &buffer[0];
char* const block_end = block_start + sizeof(buffer);

typedef long streamsize;
streamsize const m_total_allocated = sizeof(buffer);

// Constructor.

// Common.
_Atomic(int) m_resetting = 0;
_Atomic(char*) m_last_pptr = block_start;       // The producer thread ONLY writes to this variable (pptr). The consumer thread ONLY reads it.
_Atomic(char*) m_last_gptr = block_start;       // The producer thread ONLY read this variable. The consumer thread ONLY writes to it (gptr).
_Atomic(streamsize) m_total_read = 0;

// Util.
streamsize min(streamsize a, streamsize b)
{
  return a < b ? a : b;
}

// Producer thread.
streamsize m_total_reset = 0;
char* pptr_val = block_start;

// Consumer thread.
char* gptr_val = block_start;
char* egptr_val = block_start;

// Producer thread.
char* pbase()
{
  return block_start;
}

// Producer thread.
char* pptr()
{
  return pptr_val;
}

// Producer thread.
char* epptr()
{
  return block_end;
}

// Producer thread.
void pbump(streamsize n)
{
  pptr_val += n;
}

// Consumer thread.
char* gptr()
{
  return gptr_val;
}

// Consumer thread.
char* egptr()
{
  return egptr_val;
}

// Consumer thread.
void streambuf_gbump(streamsize n)
{
  gptr_val += n;
}

// Consumer thread.
void setg(char* begin, char* p, char* end)
{
  assert(begin == block_start);
  gptr_val = p;
  egptr_val = end;
}

struct MemoryBlock
{
};

typedef struct MemoryBlock MemoryBlock;

char* release_memory_block(MemoryBlock* block) { assert(0); return block_start; }
char* MemoryBlock_block_start(MemoryBlock* this) { return block_start; }
size_t MemoryBlock_get_size(MemoryBlock* this) { return block_end - block_start; }

MemoryBlock m_get_area_block_node_instance;
MemoryBlock* m_get_area_block_node = &m_get_area_block_node_instance;

// INCLUDES_BEGIN
#include "genmc_sync_egptr.h"
#include "genmc_store_last_gptr.h"
#include "genmc_unused_in_last_block.h"
#include "genmc_get_data_size.h"
#include "genmc_update_get_area.hc"
#include "genmc_update_put_area.hc"
#include "genmc_xsgetn_a.hc"
// INCLUDES_END

int PRODUCER_RESET_AFTER_TWO_BYTES = 0;
int BEING_RESET_BEFORE_SYNC = 0;

void* producer_thread(void* param)
{
  streamsize available;
  char* cur_pptr;

  //-------------------------------------------------
  // Write 1 character.

  cur_pptr = update_put_area(&available);
  assert(available == 4);
  assert(cur_pptr == pptr_val);
  assert(cur_pptr == block_start);

  // Write two characters to the buffer.
  pptr_val[0] = 'A';
  pptr_val[1] = 'B';
  pbump(2);
  assert(pptr_val == block_start + 2);

  //-------------------------------------------------
  // flush it.
  sync_egptr(pptr_val);
  // Only the producer thread writes to m_last_pptr (and well in sync_egptr).
  assert(m_last_pptr == pptr_val);

  //-------------------------------------------------

  // At this point the consumer thread might or might not
  // read 2 characters from the buffer. If it does and it
  // updates m_last_gptr in time, then the next call to
  // update_put_area will initiate a buffer reset, otherwise
  // not.

  //-------------------------------------------------
  // Write 1 character.

  cur_pptr = update_put_area(&available);
  assert(available == 2 || available == 4);

  // If the buffer was reset then now there would be again 4 bytes available.
  PRODUCER_RESET_AFTER_TWO_BYTES = available == 4;

  assert(cur_pptr == pptr_val);
  assert(cur_pptr + available == block_end);
  assert((PRODUCER_RESET_AFTER_TWO_BYTES && pptr_val == block_start) ||
         (!PRODUCER_RESET_AFTER_TWO_BYTES && pptr_val == block_start + 2));

  // m_last_pptr wasn't changed by update_put_area.
  assert(m_last_pptr == pptr_val);

  // Write a third character to the buffer.
  *pptr_val = 'C';
  pbump(1);

  //-------------------------------------------------
  // flush it.
  sync_egptr(pptr_val);
  // Only the producer thread writes to m_last_pptr (and well in sync_egptr).
  assert(m_last_pptr == pptr_val);

  return NULL;
}

void increment_total_read(streamsize n)
{
  // Memory orders are not copied from C++ code!  See StreamBufConsumer::xsgetn_a and StreamBufConsumer::gbump.
  streamsize new_total_read = atomic_load_explicit(&m_total_read, memory_order_relaxed) + n;
  atomic_store_explicit(&m_total_read, new_total_read, memory_order_release);
}

streamsize my_xsgetn_a(char* s, streamsize const n)
{
  char* cur_gptr;
  streamsize available = 0;

  for (int loop = 0; available == 0 && loop < 2; ++loop)
  {
    int at_end_and_has_next_block = update_get_area(&m_get_area_block_node, &cur_gptr, &available);
    // The gptr is entirely consumer thread side.
    // update_get_area might have updated it, but always returns the current gptr value.
    assert(cur_gptr == gptr_val);
    // This whole test only uses a single memory block. There is never a 'next' block.
    assert(!at_end_and_has_next_block);
    // Therefore, get_area_block_node is never updated to point to a 'next' block either.
    assert(m_get_area_block_node == &m_get_area_block_node_instance);
  }

  if (available == 0)
    return 0;

  // Read the character.
  streamsize remaining = n;

  while (remaining)
  {
    s[n - remaining] = gptr_val[n - remaining];
    --remaining;
    if (--available == 0)
      break;
  }

  streambuf_gbump(n - remaining);   // Do not update m_total_read.

  // Pass it on to the consumer thread, so that can detect if the buffer is now empty or not.
  store_last_gptr(cur_gptr + n - remaining);

  increment_total_read(n - remaining);

  return n - remaining;
}

int CONSUMER_RESET_AFTER_TWO_BYTES = 0;
char read_buffer[3] = { 0, 0, 0 };
streamsize rlen = 0;

void* consumer_thread(void* param)
{
  // This call to xsgetn_a() reads zero or two characters; so once it returns
  // gptr is always at block_start or block_start + 2.
  // Even if in the meantime the producer thread initiated a buffer reset,
  // that only means the put area could be reset to the beginning of the buffer
  // in the mean time.
  // The get area won't be reset until the next call to update_get_area.
  rlen = my_xsgetn_a(&read_buffer[0], 2);
  assert(rlen == 0 || rlen == 2);
  assert(gptr_val == block_start + rlen);

  // Read one character.
  rlen += my_xsgetn_a(&read_buffer[rlen], 1);

  CONSUMER_RESET_AFTER_TWO_BYTES = rlen == 3 && gptr_val == block_start + 1;

  // If the buffer was reset then the value would be block_start + 1, otherwise block_start + 3.
  assert(rlen != 3 || CONSUMER_RESET_AFTER_TWO_BYTES || gptr_val == block_start + 3);

  return NULL;
}

int main()
{
  pthread_t t1, t2;

  pthread_create(&t1, NULL, producer_thread, NULL);
  pthread_create(&t2, NULL, consumer_thread, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  // Both threads agree on whether or not there was a reset.
  assert(!CONSUMER_RESET_AFTER_TWO_BYTES || PRODUCER_RESET_AFTER_TWO_BYTES);

  // We wrote 3 bytes.
  assert(pptr_val == block_start + 3 - m_total_reset);
  // We always do a sync_egptr after writing.
  assert(m_last_pptr == pptr_val);
  // We always do store_last_gptr after reading.
  assert(m_last_gptr == gptr_val);
  // Still available in the buffer. If we are in the middle of a reset then of course pptr and ggptr are out of sync.
  assert((pptr_val - gptr_val == 3 - rlen) || m_resetting);
  // The number of characters now in the buffer are those that weren't read.
  assert(get_data_size() == 3 - rlen);

  // Check for sane values of m_total_reset.
  if (rlen == 3)
  {
    // At most one reset took place.
    assert(m_total_reset == 0 || m_total_reset == 2);

    assert(read_buffer[0] == 'A');
    assert(read_buffer[1] == 'B');
    assert(read_buffer[2] == 'C');
  }

  if (rlen == 2)
  {
    // At most one reset took place. If a reset is in progress than m_total_reset was already set to 1.
    assert((m_total_reset == 0 && !m_resetting) || m_total_reset == 2);

    assert(read_buffer[0] == 'A');
    assert(read_buffer[1] == 'B');
  }

  if (rlen == 1)
  {
    // The first read failed. The second succeeded. So no reset took place.
    assert(m_total_reset == 0);

    assert(read_buffer[0] == 'A');
  }

  if (rlen == 0)
  {
    // This would mean that both reads were done before any write. Therefore no reset took place.
    assert(m_total_reset == 0);
  }

  return 0;
}
