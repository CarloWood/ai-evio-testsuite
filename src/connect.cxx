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
using evio::GetThread;
template<threadpool::Timer::time_point::rep count, typename Unit> using Interval = threadpool::Interval<count, Unit>;

// This decoder is not really used because we don't send anything over the socket.
// However, at least one of input(decoder) or output(stream) must be provided
// for each socket, otherwise an assert happens (normally it makes little sense
// to create a Socket that does no I/O at all).
class MyDecoder : public InputDecoder
{
 private:
#ifdef CWDEBUG
  size_t m_received;
#endif

 public:
  MyDecoder() CWDEBUG_ONLY(: m_received(0)) { }

 protected:
  RefCountReleaser decode(MsgBlock&& msg, GetThread) override;
};

// This is the type of the accepted sockets when a new client connects to our listen socket.
// It registers both input and output - but also doesn't write anything.
class MyAcceptedSocket : public Socket
{
 private:
  MyDecoder m_input;
  OutputStream m_output;

 public:
  MyAcceptedSocket()
  {
    input(m_input);
    output(m_output);
  }

 protected:
  virtual RefCountReleaser read_returned_zero()
  {
    Dout(dc::notice, "*** DISCONNECTED ***");
    return close_input_device();
  }
};

// The type of our listen socket: for each incoming connection a MyAcceptedSocket is spawned.
class MyListenSocket : public ListenSocket<MyAcceptedSocket>
{
 protected:
  void new_connection(accepted_socket_type& UNUSED_ARG(accepted_socket))
  {
    Dout(dc::notice, "New connection to listen socket was accepted.");
  }
};

// The type of the socket that we use to connect to our own listen socket.
// It detects that the 'connected()' signal is received even though we
// never write anything to the socket.
class MySocket : public Socket
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

  utils::VTPtr<Socket, InputDevice, OutputDevice> VT_ptr;

 private:
  bool m_connected;

 public:
  MySocket() : VT_ptr(this), m_connected(false) { }
  ~MySocket() { ASSERT(m_connected); }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  AIThreadPool thread_pool;
  [[maybe_unused]] AIQueueHandle high_priority_handler = thread_pool.new_queue(32);
  [[maybe_unused]] AIQueueHandle medium_priority_handler = thread_pool.new_queue(32);
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  static SocketAddress const listen_address("0.0.0.0:9002");

  try
  {
    // Initialize the IO event loop thread.
    evio::EventLoop event_loop(low_priority_handler);

    // Start a listen socket on port 9002 that is closed after 10 seconds.
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

  Dout(dc::notice, "Leaving main...");
}

evio::RefCountReleaser MyDecoder::decode(MsgBlock&& CWDEBUG_ONLY(msg), GetThread)
{
  RefCountReleaser need_allow_deletion;
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode(\"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  Debug(m_received += msg.get_size());
  return need_allow_deletion;
}
