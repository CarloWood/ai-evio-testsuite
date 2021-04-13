#include "evio/ListenSocket.h"
#include "evio/AcceptedSocket.h"
#include "evio/EventLoop.h"
#include "threadpool/Timer.h"
#include "utils/Signals.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

namespace test_socket {

size_t constexpr burst_size = 1000000;     // Write this many times 100 bytes.

using evio::MsgBlock;

class MySocketTestDecoder : public evio::protocol::Decoder
{
 private:
  size_t m_received;

 public:
  MySocketTestDecoder() : m_received(0) { }
  ~MySocketTestDecoder() { Dout(dc::notice, "~MySocketTestDecoder() [" << this << "]"); }

 protected:
  void decode(int& allow_deletion_count, MsgBlock&& msg) override;

  size_t minimum_block_size_estimate() const override { return 4096; }
};

class MyOutputStream4096 : public evio::OutputStream // Protocol
{
 protected:
  size_t minimum_block_size_estimate() const override { return 4096; }
};

using MyAcceptedSocket = evio::AcceptedSocket<MySocketTestDecoder, MyOutputStream4096>;

class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
 public:
  void new_connection(accepted_socket_type& accepted_socket) override
  {
    Dout(dc::notice, "New connection to listen socket was accepted. Sending " << (100 * burst_size) << " of data.");
    // Write 10 Mbyte of data.
    for (size_t n = 0; n < burst_size; ++n)
      accepted_socket() << "START012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789END.\n";
    accepted_socket() << std::flush;
    accepted_socket.flush_output_device();
    close();
  }
  MyListenSocket() = default;
  ~MyListenSocket() { Dout(dc::notice, "~MyListenSocket() [" << this << "]"); }
};

class MyClientSocket : public evio::Socket
{
 public:
  void connected(int& UNUSED_ARG(allow_deletion_count), bool DEBUG_ONLY(success))
  {
    Dout(dc::notice, (success ? "*** CONNECTED ***" : "*** FAILED TO CONNECT ***"));
    ASSERT((m_connected_flags & (is_connected|is_disconnected)) == (success ? is_connected : is_disconnected));
    m_connected = true;
  }

 private:
  bool m_connected;

 public:
  MyClientSocket() : m_connected(false)
  {
    Dout(dc::notice, "MyClientSocket() [" << this << "]");
    on_connected([this](int& allow_deletion_count, bool success){ connected(allow_deletion_count, success); });
  }

  ~MyClientSocket()
  {
    Dout(dc::notice, "~MyClientSocket() [" << this << "]");
    ASSERT(m_connected);
  }
};

void MySocketTestDecoder::decode(int& allow_deletion_count, MsgBlock&& msg)
{
  // Just print what was received.
  DoutEntering(dc::notice, "MySocketTestDecoder::decode(\"{" << allow_deletion_count << "}, " << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  m_received += msg.get_size();
  Dout(dc::notice, "m_received = " << m_received);
  // Stop when the last message was received.
  if (m_received == 100 * burst_size)
    close_input_device(allow_deletion_count);
}

template<threadpool::Timer::time_point::rep count, typename Unit> using Interval = threadpool::Interval<count, Unit>;

} // namespace test_socket

TEST(Socket, Constructor)
{
  using namespace test_socket;

  // Initialize signals. SIGPIPE *must* be ignored or write() won't return EPIPE when peer closed the connection.
  utils::Signals signals({SIGPIPE});

  // Create a thread pool.
  AIThreadPool thread_pool;
  // Create a new queue with a capacity of 32 and default priority.
  AIQueueHandle queue_handle = thread_pool.new_queue(32);

  try
  {
    // Construct MySocketTestDecoder before EventLoop, so that the EventLoop is destructed first!
    // Otherwise the decoder is destructed before we can use it (in the destructor of event_loop).
    //MySocketTestDecoder decoder;

    // Initialize the IO event loop thread.
    evio::EventLoop event_loop(queue_handle);

    // Start a listen socket on port 9001 that is closed after 10 seconds.
    static evio::SocketAddress const listen_address("0.0.0.0:9001");
    {
      auto listen_sock = evio::create<MyListenSocket>();
      listen_sock->listen(listen_address);
    }

    // Dumb way to wait until the listen socket is up.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    {
      // Connect a socket to the listen socket.
      auto socket = evio::create<MyClientSocket>();
      socket->set_source(socket, 1024 - evio::block_overhead_c, 4096, 1000000);
      //socket->set_sink(decoder, 4096, 100000000);
      socket->connect(listen_address);
      //socket->flush_output_device();
    }

    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
  catch (std::exception const& error)
  {
    DoutFatal(dc::core, error.what());
  }
}
