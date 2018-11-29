#include "sys.h"
#include "debug.h"
#include "threadpool/AIThreadPool.h"
#include "evio/EventLoopThread.h"
#include "evio/ListenSocket.h"
#include "evio/Socket.h"
#include "threadpool/Timer.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

using evio::InputDecoder;
using evio::InputBuffer;
using evio::OutputBuffer;
using evio::MsgBlock;
using evio::OutputStream;
using evio::Socket;
using evio::ListenSocket;
using evio::RefCountReleaser;
using evio::SocketAddress;
template<threadpool::Timer::time_point::rep count, typename Unit> using Interval = threadpool::Interval<count, Unit>;

class Decoder : public InputDecoder
{
 private:
  size_t m_received;

 public:
  Decoder() : m_received(0) { }

 protected:
  RefCountReleaser decode(MsgBlock msg) override;
};

class MyListenSocket : public ListenSocket<Decoder, OutputStream>
{
 protected:
  void new_connection(OutputStream& connection)
  {
    Dout(dc::notice, "New connection to listen socket was accepted. Sending 10 kb of data.");
    // Write 10 kbyte of data.
    for (int n = 0; n < 100; ++n)
      connection << "START012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789THEEND" << std::endl;
  }
};

class BurstSocket : public Socket
{
  private:
   Decoder m_input;
   OutputStream m_output;

  public:
   BurstSocket()
   {
     input(m_input);
     output(m_output);
   }

   void write_burst()
   {
     // The flush is necessary, it signals to evio that there is something
     // in the buffer and starts the output device.
     m_output << "Burst data!" << std::flush;
   }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  AIThreadPool thread_pool;
  [[maybe_unused]] AIQueueHandle high_priority_handler = thread_pool.new_queue(32);
  [[maybe_unused]] AIQueueHandle medium_priority_handler = thread_pool.new_queue(32);
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  // Initialize the IO event loop thread.
  EventLoopThread::instance().init(low_priority_handler);

  static SocketAddress const listen_address("0.0.0.0:9001");

  try
  {
    // Start a listen socket on port 9001 that is closed after 10 seconds.
    threadpool::Timer timer;
    {
      auto listen_sock = evio::create<MyListenSocket>();
      listen_sock->listen(listen_address);
      timer.start(Interval<10, std::chrono::seconds>(),
          [&timer, listen_sock]()
          {
            timer.release_callback();
            listen_sock->close();
            EventLoopThread::instance().bump_terminate();
          });
    }

    // Dumb way to wait until the listen socket is up.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
      // Connect 100 sockets to it.
      std::array<boost::intrusive_ptr<BurstSocket>, 100> sockets;
      for (size_t s = 0; s < sockets.size(); ++s)
        sockets[s] = evio::create<BurstSocket>();
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      for (size_t s = 0; s < sockets.size(); ++s)
        sockets[s]->connect(listen_address);
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      for (size_t s = 0; s < sockets.size(); ++s)
        sockets[s]->write_burst();
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      for (size_t s = 0; s < sockets.size(); ++s)
        sockets[s]->write_burst();
    }

    // Wait until all watchers have finished.
    EventLoopThread::instance().terminate();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}

evio::RefCountReleaser Decoder::decode(MsgBlock msg)
{
  RefCountReleaser releaser;
  // Just print what was received.
  DoutEntering(dc::notice, "Decoder::decode(\"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  m_received += msg.get_size();
  // Stop when the last message was received.
  if (m_received == 10200)
    releaser = stop_input_device();
  return releaser;
}
