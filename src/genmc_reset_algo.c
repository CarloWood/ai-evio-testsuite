// Install https://github.com/MPI-SWS/genmc
//
// Then test with:
//
// genmc genmc_reset_algo.c

// These header files are replaced by genmc (see /usr/local/include/genmc):
#include <pthread.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <stdatomic.h>
//#include <stdio.h>

// Initialization:
_Atomic(int) m_resetting = 0;   // Not resetting yet.
_Atomic(int) m_last_pptr = 5;   // Wrote 5 bytes to the buffer.
int gptr = 5;                   // Read 5 bytes: the buffer is empty.

int total_written = 0;

void* producer_thread(void* param)
{
  // Initiate a buffer reset:
  atomic_store_explicit(&m_last_pptr, 0, memory_order_relaxed);
  atomic_store_explicit(&m_resetting, 1, memory_order_release);

  // Next write more data:
  atomic_store_explicit(&m_last_pptr, 2, memory_order_release);      // Write more data, but less than used to be in the buffer.
  atomic_store_explicit(&m_last_pptr, 8, memory_order_release);      // Write even more, causing pptr to become larger than gptr.

  total_written = atomic_load_explicit(&m_last_pptr, memory_order_relaxed);
  return NULL;
}

int total_read = 0;

void* consumer_thread(void* param)
{
  // Detect a reset (or note that the buffer is empty).
  int is_resetting = atomic_load_explicit(&m_resetting, memory_order_acquire);
  int cur_pptr = atomic_load_explicit(&m_last_pptr, memory_order_acquire);
  is_resetting |= (cur_pptr == 0 && gptr != 0);
  if (!is_resetting && atomic_load_explicit(&m_resetting, memory_order_acquire))
  {
    is_resetting = 1;
    cur_pptr = atomic_load_explicit(&m_last_pptr, memory_order_relaxed);
  }

  // At this point either the reset was seen, or it was not seen.
  // If it was not seen then is_resetting == 0 and cur_pptr MUST be equal to 5 (the old value).
  if (!is_resetting)
  {
    assert(cur_pptr == 5);
    // Buffer is emtpy, so read nothing.
  }
  else
  {
    // pptr was reset.
    assert(cur_pptr == 0 || cur_pptr == 2 || cur_pptr == 8);
    total_read += cur_pptr; // In this everything is read. See below.
    atomic_store_explicit(&m_resetting, 0, memory_order_relaxed);       // Can be relaxed because this thread is the only thread that reads it.
    gptr = 0;
  }

  // Always read everything up till cur_pptr.
  gptr = cur_pptr;

  return NULL;
}

int main()
{
  pthread_t t1, t2;

  pthread_create(&t1, NULL, producer_thread, NULL);
  pthread_create(&t2, NULL, consumer_thread, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  // Afterwards total_written bytes were written and total_read were read,
  // so the data remaining in the buffer is...
  assert(atomic_load_explicit(&m_last_pptr, memory_order_relaxed) -
         (atomic_load_explicit(&m_resetting, memory_order_relaxed) ? 0 : gptr) ==
         total_written - total_read);

  return 0;
}
