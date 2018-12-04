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

// This decoder is not really used because we don't send anything over the socket.
// However, at least one of input(decoder) or output(stream) must be provided
// for each socket, otherwise an assert happens (normally it makes little sense
// to create a Socket that does no I/O out at all).
class MyDecoder : public InputDecoder
{
 private:
  size_t m_received;

 public:
  MyDecoder() : m_received(0) { }

 protected:
  RefCountReleaser decode(MsgBlock msg) override;
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
 private:
  bool m_connected;

  void connected() override
  {
    Dout(dc::notice, "*** CONNECTED ***");
    m_connected = true;
    // By immediately disconnecting again we cause a connection reset by peer on the otherside,
    // which causes MyAcceptedSocket::read_returned_zero() to be called. See above.
    close();
  }

 public:
  MySocket() : m_connected(false) { }
  ~MySocket() { ASSERT(m_connected); }
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

  static SocketAddress const listen_address("0.0.0.0:9002");

  try
  {
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
            EventLoopThread::instance().bump_terminate();
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

    // Wait until all watchers have finished.
    EventLoopThread::instance().terminate();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }

  Dout(dc::notice, "Leaving main...");
}

evio::RefCountReleaser MyDecoder::decode(MsgBlock msg)
{
  RefCountReleaser releaser;
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode(\"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  return releaser;
}
