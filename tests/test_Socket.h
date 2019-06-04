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

class MySocket : public evio::Socket
{
 public:
  using VT_type = Socket::VT_type;

  struct VT_impl : Socket::VT_impl
  {
    // Override
    static void connected(Socket* _self, bool CWDEBUG_ONLY(success))
    {
      MySocket* self = static_cast<MySocket*>(_self);
      Dout(dc::notice, (success ? "*** CONNECTED ***" : "*** FAILED TO CONNECT ***"));
      self->m_connected = true;
      // By immediately disconnecting again we cause a connection reset by peer on the otherside,
      // which causes MyAcceptedSocket::read_returned_zero() to be called. See above.
      self->close();
    }

    static constexpr VT_type VT{
      /*Socket*/
        /*InputDevice*/
      { nullptr,
        read_from_fd,
        read_returned_zero,
        read_error,
        data_received },
        /*OutputDevice*/
      { nullptr,
        write_to_fd,
        write_error },
      connected,
      disconnected
    };
  };

  // Make a deep copy of VT_ptr.
  VT_type* clone_VT() override { return VT_ptr.clone(this); }

  utils::VTPtr<MySocket, Socket> VT_ptr;

 private:
  bool m_connected;
  MyDecoder m_decoder;

 public:
  MySocket() : VT_ptr(this), m_connected(false) { Dout(dc::notice, "MySocket::VT has address: " << (void*)&VT_impl::VT); }
  ~MySocket() { ASSERT(m_connected); }
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
    // Construct Timer before EventLoop, so that the EventLoop is destructed first!
    // Otherwise the timer is destructed and cancelled before we even start to wait for it (in the destructor of event_loop).
    threadpool::Timer timer;
    // Initialize the IO event loop thread.
    evio::EventLoop event_loop(queue_handle);

    // Start a listen socket on port 9001 that is closed after 10 seconds.
    static evio::SocketAddress const listen_address("0.0.0.0:9001");
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

    // Dumb way to wait until the listen socket is up.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    MyDecoder decoder;
    {
      // Connect a socket to the listen socket.
      auto socket = evio::create<MySocket>();
      socket->input(decoder);
      socket->connect(listen_address);
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
