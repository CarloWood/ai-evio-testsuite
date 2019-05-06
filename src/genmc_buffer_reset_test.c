// Install https://github.com/MPI-SWS/genmc
//
// Then test with:
//
// genmc -unroll=3 -- genmc_buffer_reset_test.c

// These header files are replaced by genmc (see /usr/local/include/genmc):
#include <pthread.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <stdatomic.h>
//#include <stdio.h>

atomic_int n = -1;
atomic_int n2 = 0;

void* thread_1(void* param)
{
  n2 = 1;
  if (n != -1)
    atomic_store_explicit(&n, 1, memory_order_release);

  return NULL;
}

void* thread_2(void* param)
{
  n = 0;
  int expected = 0;
  int desired;
  do
  {
    desired = n2;
  }
  while (!atomic_compare_exchange_strong_explicit(&n, &expected, desired, memory_order_acquire, memory_order_relaxed));

  return NULL;
}

int main()
{
  pthread_t t1, t2;

  pthread_create(&t1, NULL, thread_1, NULL);
  pthread_create(&t2, NULL, thread_2, NULL);

  pthread_join(t1, NULL);
  pthread_join(t2, NULL);

  assert(n == 1);

  return 0;
}
