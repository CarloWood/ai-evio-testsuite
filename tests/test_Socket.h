#include "evio/ListenSocket.h"
#include "threadpool/Timer.h"
#include <libcwd/buf2str.h>

class MyDecoder : public evio::InputDecoder
{
 private:
  size_t m_received;

 public:
  MyDecoder() : m_received(0) { }

 protected:
  evio::RefCountReleaser decode(evio::MsgBlock&& msg, evio::GetThread type) override;
};

class MyAcceptedSocket : public evio::Socket
{
 private:
  MyDecoder m_input;
  evio::OutputStream m_output;

 public:
  MyAcceptedSocket()
  {
    input(m_input);
    output(m_output);
  }

  evio::OutputStream& operator()() { return m_output; }
};

class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
 protected:
  void new_connection(accepted_socket_type& accepted_socket)
  {
    Dout(dc::notice, "New connection to listen socket was accepted. Sending 10 kb of data.");
    // Write 10 kbyte of data.
    for (int n = 0; n < 100; ++n)
      accepted_socket() << "START012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789THEEND" << std::endl;
  }
};

template<threadpool::Timer::time_point::rep count, typename Unit> using Interval = threadpool::Interval<count, Unit>;

TEST(Socket, Constructor)
{
  // Create a thread pool.
  AIThreadPool thread_pool;
  // Create a new queue with a capacity of 32 and default priority.
  AIQueueHandle queue_handle = thread_pool.new_queue(32);

  try
  {
    // Initialize the IO event loop thread.
    evio::EventLoop event_loop(queue_handle);

    // Start a listen socket on port 9001 that is closed after 10 seconds.
    static evio::SocketAddress const listen_address("0.0.0.0:9001");
    threadpool::Timer timer;
    {
      auto listen_sock = evio::create<MyListenSocket>();
      listen_sock->listen(listen_address);
      timer.start(Interval<10, std::chrono::seconds>(),
          [&timer, listen_sock]()
          {
            timer.release_callback();
            listen_sock->close();
            evio::EventLoopThread::instance().bump_terminate();
          });
    }

    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}

evio::RefCountReleaser MyDecoder::decode(evio::MsgBlock&& msg, evio::GetThread)
{
  evio::RefCountReleaser need_allow_deletion;
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode(\"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  m_received += msg.get_size();
  // Stop when the last message was received.
  if (m_received == 10200)
    need_allow_deletion = stop_input_device();
  return need_allow_deletion;
}
