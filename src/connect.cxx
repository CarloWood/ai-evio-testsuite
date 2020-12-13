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

using evio::protocol::Decoder;
using evio::InputBuffer;
using evio::OutputBuffer;
using evio::MsgBlock;
using evio::OutputStream;
using evio::SocketAddress;
using evio::GetThread;
template<threadpool::Timer::time_point::rep count, typename Unit> using Interval = threadpool::Interval<count, Unit>;

class MyDecoder : public Decoder
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
  void read_returned_zero(int& allow_deletion_count) override
  {
    Dout(dc::notice, "*** DISCONNECTED ***");
    evio::InputDevice::read_returned_zero(allow_deletion_count);
  }
};

// The type of our listen socket: for each incoming connection a MyAcceptedSocket is spawned.
class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
 public:
  // Called when a new connection is accepted.
  void new_connection(accepted_socket_type& UNUSED_ARG(accepted_socket)) override
  {
    Dout(dc::notice, "New connection to listen socket was accepted.");
    close();
  }
};

// The type of the socket that we use to connect to our own listen socket.
// It detects that the 'connected()' signal is received even though we
// never write anything to the socket.
class MySocket : public evio::Socket
{
 public:
  void connected(int& allow_deletion_count, bool DEBUG_ONLY(success))
  {
    Dout(dc::notice, (success ? "*** CONNECTED ***" : "*** FAILED TO CONNECT ***"));
    ASSERT((m_connected_flags & (is_connected|is_disconnected)) == (success ? is_connected : is_disconnected));
    m_connected = true;
    // By immediately disconnecting again we cause a connection reset by peer on the otherside,
    // which causes MyAcceptedSocket::read_returned_zero() to be called. See above.
    close(allow_deletion_count);
  }

  MySocket() : m_connected(false)
  {
    onConnected([this](int& allow_deletion_count, bool success){ connected(allow_deletion_count, success); });
  }

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
      socket->set_protocol_decoder(decoder);
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
