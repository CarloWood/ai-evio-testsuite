#include "sys.h"
#include "evio/evio.h"
#include "evio/EventLoopThread.h"
#include "evio/FileDescriptor.h"
#include "debug.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <chrono>
#include <atomic>

// Callback for a time-out.
static void timeout_cb(EV_P_ ev_timer* UNUSED_ARG(w), int UNUSED_ARG(revents))
{
  Dout(dc::notice, "Calling timeout_cb()!");
  // Called from EventLoopThread::invoke_pending, hence m_loop_mutex is already locked.
  // This causes the innermost nested ev_run's to stop iterating.
  ev_break(EV_A_ EVBREAK_ONE);
}

// Callback for stdin.
static void stdin_cb(EV_P_ ev_io* w, int UNUSED_ARG(revents))
{
  Dout(dc::notice, "Calling stdin_cb()");
  std::string line;
  std::getline(std::cin, line);
  // Called from EventLoopThread::invoke_pending, hence m_loop_mutex is already locked.
  // For one-shot events, one must manually stop the watcher with its corresponding stop function.
  ev_io_stop(EV_A_ w);
  // This causes all nested ev_run's to stop iterating.
  ev_break(EV_A_ EVBREAK_ALL);
}

struct MyDummyDevice : public evio::FileDescriptor
{
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  AIThreadPool thread_pool;
  [[maybe_unused]] AIQueueHandle high_priority_handler = thread_pool.new_queue(32);
  [[maybe_unused]] AIQueueHandle medium_priority_handler = thread_pool.new_queue(32);
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  // Create the IO event loop thread.
  evio::EventLoopThread::instance().init(low_priority_handler);

  // Fill the threadpool queue.
  for (int i = 0; i < 1000; ++i)
  {
    auto queues_access = thread_pool.queues_read_access();
    auto& queue = thread_pool.get_queue(queues_access, low_priority_handler);
    bool queue_full;
    do
    {
      {
        auto queue_access = queue.producer_access();
        static int queue_counter = 99;
        int len = queue_access.length();
        if (!(queue_full = len == queue.capacity()))
        {
          int qc = ++queue_counter;
          Dout(dc::notice, "Adding #" << qc << " to the queue with length " << len);
          queue_access.move_in([qc, len](){
              Dout(dc::notice|flush_cf, "Thread pool worker executing entry #" << qc << " (sleeping 1 second).");
              std::this_thread::sleep_for(std::chrono::seconds(1));
              return false; });
        }
      }
      if (!queue_full)
        queue.notify_one();
    }
    while (!queue_full);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Add a timer watcher.
  ev_timer timeout_watcher;
  ev_timer_init(&timeout_watcher, timeout_cb, 4.0, 0.);
  evio::EventLoopThread::instance().start(timeout_watcher);

  // Add stdin watcher.
  MyDummyDevice not_disabled;
  ev_io stdin_watcher;
  ev_io_init(&stdin_watcher, stdin_cb, /*STDIN_FILENO*/ 0, EV_READ);
  evio::EventLoopThread::instance().start(&stdin_watcher, &not_disabled);

  // Wait until all watchers have finished.
  evio::EventLoopThread::instance().terminate();
}
