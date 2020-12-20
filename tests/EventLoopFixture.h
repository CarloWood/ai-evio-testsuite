#pragma once

#include "evio/EventLoop.h"
#include "threadpool/Timer.h"

template<typename BASE = testing::Test>
class EventLoopFixture : public BASE
{
 private:
  AISignals m_signals;
  AIThreadPool m_thread_pool;
  evio::EventLoop* m_event_loop;
  AIQueueHandle m_low_priority_handler;
  bool m_clean_exit;

 public:
  threadpool::Timer* m_timer;

 protected:
  void SetUp() override
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v EventLoopFixture::SetUp()");
    debug::Mark setup;
#endif
    utils::Signals::instance().reserve({SIGPIPE}, 0);
    if constexpr (!std::is_same_v<BASE, testing::Test>)
      BASE::SetUp();
    [[maybe_unused]] AIQueueHandle high_priority_handler = m_thread_pool.new_queue(32);
    [[maybe_unused]] AIQueueHandle medium_priority_handler = m_thread_pool.new_queue(32);
    m_low_priority_handler = m_thread_pool.new_queue(16);
    utils::Signals::unblock(SIGPIPE);   // Unblock sigpipe for the event handler thread.
    m_timer = new threadpool::Timer;
    m_event_loop = new evio::EventLoop(m_low_priority_handler);
    m_clean_exit = true;
  }

  void destruct_thread_pool()
  {
    m_thread_pool.change_number_of_threads_to(0);
    AIThreadPool dummy(std::move(m_thread_pool));
  }

  void TearDown() override
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v EventLoopFixture::TearDown()");
    debug::Mark setup;
#endif
    // Fake "leaving the scope" of AIThreadPool and EventLoop...
    // Don't use this yourself.
    // The correct way to do this is:
    //
    // {
    //   AIThreadPool thread_pool;
    //   AIQueueHandle handler = thread_pool.new_queue(32);     // For example.
    //   EventLoop event_loop(handler);
    //
    //   ...your code here...
    //
    //   event_loop.join();
    // }

    if (m_clean_exit)
      m_event_loop->join();
    delete m_event_loop;
    delete m_timer;
    m_low_priority_handler.set_to_undefined();
    destruct_thread_pool();
    if constexpr (!std::is_same_v<BASE, testing::Test>)
      BASE::TearDown();
    utils::Signals::instance().block_and_unregister(SIGPIPE);
  }

  void force_exit()
  {
    m_clean_exit = false;
  }
};

