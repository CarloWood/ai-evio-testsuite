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

char dummy;
char* const uninitialized = &dummy;
char buffer[4] = { 0, 0, 0, 0 };
char* const block_start = &buffer[0];
char* const block_end = block_start + sizeof(buffer);

typedef long streamsize;

streamsize const m_total_allocated = block_end - block_start;

// Constructor.

// Common.
_Atomic(char*) m_next_egptr = block_start;
_Atomic(char*) m_next_egptr2 = uninitialized;   // The producer thread ONLY writes to this variable (pptr). The consumer thread ONLY reads it.
_Atomic(char*) m_last_gptr = block_start;       // The producer thread ONLY read this variable. The consumer thread ONLY writes to it (gptr).
_Atomic(streamsize) m_total_read = 0;

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

#include "genmc_sync_egptr.c"
#include "genmc_update_put_area.c"
#include "genmc_store_last_gptr.c"
#include "genmc_unused_in_last_block.c"
#include "genmc_get_data_size.c"
#include "genmc_update_get_area.c"

int PRODUCER_RESET_AFTER_ONE_BYTE = 0;
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

  // Write the first character to the buffer.
  *pptr_val = 'A';
  pbump(1);
  assert(pptr_val == block_start + 1);

  //-------------------------------------------------
  // flush it.
  sync_egptr(pptr_val);
  // Only the producer thread writes to m_next_egptr2 (and well in sync_egptr).
  assert(m_next_egptr2 == pptr_val);
  // Since m_next_egptr wasn't NULL, the value was also written to m_next_egptr.
  // The consumer thread only writes to m_next_egptr when it is NULL.
  // Therefore, m_next_egptr will also always be equal to pptr_val.
  assert(m_next_egptr == pptr_val && 1);

  //-------------------------------------------------

  // At this point the consumer thread might or might not
  // read 1 character from the buffer. If it does and it
  // updates m_last_gptr in time, then the next call to
  // update_put_area will initiate a buffer reset, otherwise
  // not.

  //-------------------------------------------------
  // Write 1 character.

  cur_pptr = update_put_area(&available);
  assert(available == 3 || available == 4);

  // If the buffer was reset then now there would be again 4 bytes available.
  PRODUCER_RESET_AFTER_ONE_BYTE = available == 4;

  assert(cur_pptr == pptr_val);
  assert(cur_pptr + available == block_end);
  assert((PRODUCER_RESET_AFTER_ONE_BYTE && pptr_val == block_start) ||
         (!PRODUCER_RESET_AFTER_ONE_BYTE && pptr_val == block_start + 1));

  // m_next_egptr2 wasn't changed by update_put_area.
  assert(m_next_egptr2 == pptr_val);
  // Neither was m_next_egptr unless it was set to NULL because we are still being reset.
  assert((PRODUCER_RESET_AFTER_ONE_BYTE && (m_next_egptr == NULL || m_next_egptr == pptr_val /* == block_start*/)) ||
         (!PRODUCER_RESET_AFTER_ONE_BYTE && m_next_egptr == pptr_val /* == block_start + 1*/));

  // Write a second character to the buffer.
  *pptr_val = 'B';
  pbump(1);

  //-------------------------------------------------
  // flush it.
  sync_egptr(pptr_val);
  // Only the producer thread writes to m_next_egptr2 (and well in sync_egptr).
  assert(m_next_egptr2 == pptr_val);
  assert(m_next_egptr == NULL || m_next_egptr == block_start || m_next_egptr == pptr_val);

  return NULL;
}

MemoryBlock some_value;

void increment_total_read(streamsize n)
{
  // Memory orders are not copied from C++ code!  See StreamBufConsumer::xsgetn_a and StreamBufConsumer::gbump.
  streamsize new_total_read = atomic_load_explicit(&m_total_read, memory_order_relaxed) + n;
  atomic_store_explicit(&m_total_read, new_total_read, memory_order_release);
}

streamsize xsgetn_a(char* s /*, streamsize const n*/)   // Always try to read exactly one character.
{
  char* cur_gptr;
  streamsize available = 0;

  for (int loop = 0; available == 0 && loop < 2; ++loop)
  {
    MemoryBlock* get_area_block_node = &some_value;
    int at_end_and_has_next_block = update_get_area(&get_area_block_node, &cur_gptr, &available);
    // The gptr is entirely consumer thread side.
    // update_get_area might have updated it, but always returns the current gptr value.
    assert(cur_gptr == gptr_val);
    // This whole test only uses a single memory block. There is never a 'next' block.
    assert(!at_end_and_has_next_block);
    // Therefore, get_area_block_node is never updated to point to a 'next' block either.
    assert(get_area_block_node == &some_value);
  }

  if (available == 0)
    return 0;

  // Read the character.
  *s = *gptr_val;
  streambuf_gbump(1);   // Do not update m_total_read.

  // Pass it on to the consumer thread, so that can detect if the buffer is now empty or not.
  store_last_gptr(cur_gptr + 1);

  increment_total_read(1);

  return 1;
}

int CONSUMER_RESET_AFTER_ONE_BYTE = 0;
char read_buffer[2] = { 0, 0 };
streamsize rlen = 0;

void* consumer_thread(void* param)
{
  // This call to xsgetn_a() reads exactly one character; so once it returns
  // gptr is always at block_start + 1. Even if in the meantime the producer
  // thread initiated a buffer reset, that only means the put area could be
  // reset to the beginning of the buffer in the mean time. The get area won't
  // be reset until the next call to update_get_area.
  rlen = xsgetn_a(&read_buffer[0]);
  assert(gptr_val == block_start + rlen);

  rlen += xsgetn_a(&read_buffer[rlen]);

  CONSUMER_RESET_AFTER_ONE_BYTE = rlen == 2 && gptr_val == block_start + 1;

  // If the buffer was reset then the value would be block_start + 1, otherwise block_start + 2.
  assert(rlen != 2 || CONSUMER_RESET_AFTER_ONE_BYTE || gptr_val == block_start + 2);

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
  assert(!CONSUMER_RESET_AFTER_ONE_BYTE || PRODUCER_RESET_AFTER_ONE_BYTE);

  // We wrote 2 bytes.
  assert(pptr_val == block_start + 2 - m_total_reset);
  // We always do a sync_egptr after writing.
  assert(m_next_egptr2 == pptr_val);
  // We always do store_last_gptr after reading.
  assert(m_last_gptr == gptr_val);
  // Still available in the buffer. If we are in the middle of a reset then of course pptr and ggptr are out of sync.
  assert((pptr_val - gptr_val == 2 - rlen) || m_next_egptr == NULL);
  // The number of characters now in the buffer are those that weren't read.
  assert(get_data_size() == 2 - rlen);

  // Check for sane values of m_total_reset and m_next_egptr.
  if (rlen == 2)
  {
    // At most one reset took place.
    assert(m_total_reset == 0 || m_total_reset == 1);
    // m_next_egptr is fully up to date.
    assert(m_next_egptr == pptr_val && rlen == 2);

    assert(read_buffer[0] == 'A');
    assert(read_buffer[1] == 'B');
  }

  if (rlen == 1)
  {
    // At most one reset took place. If a reset is in progress than m_total_reset was already set to 1.
    assert((m_total_reset == 0 && m_next_egptr != NULL) || m_total_reset == 1);
    // If the reset finished then m_next_egptr is fully up to date.
    assert(m_next_egptr == NULL || m_next_egptr == pptr_val);

    assert(read_buffer[0] == 'A');
  }

  if (rlen == 0)
  {
    // This would mean that both reads were done before any write. Therefore no reset took place.
    assert(m_total_reset == 0);
    // m_next_egptr is fully up to date.
    assert(m_next_egptr == pptr_val && rlen == 0);
  }

  return 0;
}
