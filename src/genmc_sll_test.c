// Install https://github.com/MPI-SWS/genmc
//
// Then test with:
//
// genmc -unroll 5 -- genmc_sll_test.c

// These header files are replaced by genmc (see /usr/local/include/genmc):
#include <pthread.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>

#define PRODUCER_THREADS 3
#define CONSUMER_THREADS 2

struct Node
{
  struct Node* next;
};

struct Node* const deleted = (struct Node*)0xd31373d;

_Atomic(struct Node*) list;

void* producer_thread(void* node_)
{
  struct Node* node = (struct Node*)node_;

  // Insert node at beginning of the list.
  node->next = atomic_load_explicit(&list, memory_order_relaxed);
  while (!atomic_compare_exchange_weak_explicit(&list, &node->next, node, memory_order_release, memory_order_relaxed))
    ;

  return NULL;
}

void* consumer_thread(void* param)
{
  // Replace the whole list with an empty list.
  struct Node* head = atomic_exchange_explicit(&list, NULL, memory_order_acquire);
  // Delete each node that was in the list.
  while (head)
  {
    struct Node* orphan = head;
    head = orphan->next;
    // Mark the node as deleted.
    assert(orphan->next != deleted);
    orphan->next = deleted;
  }

  return NULL;
}

pthread_t t[PRODUCER_THREADS + CONSUMER_THREADS];
struct Node n[PRODUCER_THREADS];         // Initially filled with zeroes --> none of the Node's is marked as deleted.

int main()
{
  // Start PRODUCER_THREADS threads that each append one node to the queue.
  for (int i = 0; i < PRODUCER_THREADS; ++i)
    if (pthread_create(&t[i], NULL, producer_thread, &n[i]))
      abort();

  // Start CONSUMER_THREAD threads that each delete all nodes that were added so far.
  for (int i = 0; i < CONSUMER_THREADS; ++i)
    if (pthread_create(&t[PRODUCER_THREADS + i], NULL, consumer_thread, NULL))
      abort();

  // Wait till all threads finished.
  for (int i = 0; i < PRODUCER_THREADS + CONSUMER_THREADS; ++i)
    if (pthread_join(t[i], NULL))
      abort();

  // Count number of elements still in the list.
  struct Node* l = list;
  int count = 0;
  while (l)
  {
    ++count;
    l = l->next;
  }

  // Count the number of deleted elements.
  int del_count = 0;
  for (int i = 0; i < PRODUCER_THREADS; ++i)
    if (n[i].next == deleted)
      ++del_count;

  assert(count + del_count == PRODUCER_THREADS);
  //printf("count = %d; deleted = %d\n", count, del_count);

  return 0;
}
