#include "sys.h"
#include "debug.h"
#include "evio/EventLoop.h"
#include "evio/ListenSocket.h"
#include "evio/AcceptedSocket.h"
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
using evio::SocketAddress;
using evio::GetThread;
template<threadpool::Timer::time_point::rep count, typename Unit> using Interval = threadpool::Interval<count, Unit>;

class MyDecoder : public InputDecoder
{
 private:
#ifdef CWDEBUG
  size_t m_received;
#endif

 public:
  MyDecoder() CWDEBUG_ONLY(: m_received(0)) { }

 protected:
  void decode(int& allow_deletion_count, MsgBlock&& msg) override;
};

// This is the type of the accepted socket when a new client connects to our listen socket.
// It registers both input and output - but also doesn't write anything.
class MyAcceptedSocket : public evio::AcceptedSocket<MyDecoder, OutputStream>
{
 public:
  using VT_type = evio::AcceptedSocket<MyDecoder, OutputStream>::VT_type;
  #define VT_MyAcceptedSocket VT_evio_AcceptedSocket

  struct VT_impl : evio::AcceptedSocket<MyDecoder, OutputStream>::VT_impl
  {
    // Override
    static void read_returned_zero(int& allow_deletion_count, InputDevice* _self)
    {
      Dout(dc::notice, "*** DISCONNECTED ***");
      //MyAcceptedSocket* self = static_cast<MyAcceptedSocket*>(_self);
      evio::InputDevice::VT_impl::read_returned_zero(allow_deletion_count, _self);
    }

    static constexpr VT_type VT VT_MyAcceptedSocket;
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<MyAcceptedSocket, evio::AcceptedSocket<MyDecoder, OutputStream>> VT_ptr;

  MyAcceptedSocket() : VT_ptr(this) { }
};

// The type of our listen socket: for each incoming connection a MyAcceptedSocket is spawned.
class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
 public:
  using VT_type = evio::ListenSocket<MyAcceptedSocket>::VT_type;
  #define VT_MyListenSocket VT_evio_ListenSocket

  struct VT_impl : evio::ListenSocket<MyAcceptedSocket>::VT_impl
  {
    // Called when a new connection is accepted.
    static void new_connection(evio::ListenSocket<MyAcceptedSocket>* _self, accepted_socket_type& UNUSED_ARG(accepted_socket))
    {
      Dout(dc::notice, "New connection to listen socket was accepted.");
      _self->close();
    }

    // Virtual table of ListenSocket.
    static constexpr VT_type VT VT_MyListenSocket;
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<MyListenSocket, evio::ListenSocket<MyAcceptedSocket>> VT_ptr;

  MyListenSocket() : VT_ptr(this) { }
};

// The type of the socket that we use to connect to our own listen socket.
// It detects that the 'connected()' signal is received even though we
// never write anything to the socket.
class MySocket : public evio::Socket
{
 public:
  using VT_type = evio::Socket::VT_type;
  #define VT_MySocket VT_evio_Socket

  struct VT_impl : evio::Socket::VT_impl
  {
    // Override
    static void connected(int& allow_deletion_count, evio::Socket* _self, bool DEBUG_ONLY(success))
    {
      MySocket* self = static_cast<MySocket*>(_self);
      Dout(dc::notice, (success ? "*** CONNECTED ***" : "*** FAILED TO CONNECT ***"));
      ASSERT((self->m_connected_flags & (is_connected|is_disconnected)) == (success ? is_connected : is_disconnected));
      self->m_connected = true;
      // By immediately disconnecting again we cause a connection reset by peer on the otherside,
      // which causes MyAcceptedSocket::read_returned_zero() to be called. See above.
      self->close(allow_deletion_count);
    }

    static constexpr VT_type VT VT_MySocket;
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<MySocket, Socket> VT_ptr;

  MySocket() : VT_ptr(this), m_connected(false) { }
  ~MySocket() { ASSERT(m_connected); }

 private:
  bool m_connected;
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
      socket->set_sink(decoder);
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

void MyDecoder::decode(int& CWDEBUG_ONLY(allow_deletion_count), MsgBlock&& CWDEBUG_ONLY(msg))
{
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode({" << allow_deletion_count << "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  Debug(m_received += msg.get_size());
}
