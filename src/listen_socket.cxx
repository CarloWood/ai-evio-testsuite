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
using evio::SocketAddress;
using evio::GetThread;
template<threadpool::Timer::time_point::rep count, typename Unit> using Interval = threadpool::Interval<count, Unit>;

class MyDecoder : public InputDecoder
{
 private:
  size_t m_received;

 public:
  MyDecoder() : m_received(0) { }

 protected:
  NAD_DECL(decode, MsgBlock&& msg) override;
};

class MyAcceptedSocket : public Socket
{
 private:
  MyDecoder m_input;
  OutputStream m_output;

 public:
  MyAcceptedSocket()
  {
    set_sink(m_input);
    set_source(m_output);
  }

  OutputStream& operator()() { return m_output; }
};

class MyListenSocket : public ListenSocket<MyAcceptedSocket>
{
 public:
  using VT_type = ListenSocket<MyAcceptedSocket>::VT_type;

  struct VT_impl : ListenSocket<MyAcceptedSocket>::VT_impl
  {
    // Called when a new connection is accepted.
    static void new_connection(ListenSocket* UNUSED_ARG(_self), accepted_socket_type& accepted_socket)
    {
      Dout(dc::notice, "New connection to listen socket was accepted. Sending 10 kb of data.");
      // Write 10 kbyte of data.
      for (int n = 0; n < 100; ++n)
        accepted_socket() << "START012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789THEEND" << std::endl;
    }

    // Virtual table of ListenSocket.
    static constexpr VT_type VT{
      /*ListenSocket*/
        /*ListenSocketDevice*/
      {   /*InputDevice*/
        { nullptr,
          read_from_fd,
          hup,
          exceptional,
          read_returned_zero,
          read_error,
          data_received },
        maybe_out_of_fds,
        spawn_accepted },
      new_connection       // Overridden
    };
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<MyListenSocket, ListenSocket<MyAcceptedSocket>> VT_ptr;

  MyListenSocket() : VT_ptr(this) { }
};

class BurstSocket : public Socket
{
  private:
//   MyDecoder m_input;
   OutputStream m_output;

  public:
   BurstSocket()
   {
//     set_sink(m_input);
     set_source(m_output);
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

  static SocketAddress const listen_address("0.0.0.0:9001");

  try
  {
    // Initialize the IO event loop thread.
    evio::EventLoop event_loop(low_priority_handler);

    // Start a listen socket on port 9001 that is closed after 10 seconds.
    threadpool::Timer timer;
    {
      auto listen_sock = evio::create<MyListenSocket>();
      listen_sock->listen(listen_address);
      timer.start(Interval<10, std::chrono::seconds>(),
          [&timer, listen_sock]()
          {
            timer.release_callback();
            NAD_PUBLIC_CALL(listen_sock->close);
            evio::EventLoopThread::instance().bump_terminate();
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
      {
        sockets[s]->connect(listen_address);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      for (size_t s = 0; s < sockets.size(); ++s)
        sockets[s]->write_burst();
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      for (size_t s = 0; s < sockets.size(); ++s)
      {
        sockets[s]->write_burst();
        //sockets[s]->flush_output_device();
      }
    }

    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }

  Dout(dc::notice, "Leaving main...");
}

NAD_DECL_CWDEBUG_ONLY(MyDecoder::decode, MsgBlock&& msg)
{
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode(" NAD_DoutEntering_ARG0 "\"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  m_received += msg.get_size();
  // Stop when the last message was received.
  if (m_received == 10200)
    stop_input_device();
}
