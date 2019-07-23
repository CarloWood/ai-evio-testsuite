#include "evio/ListenSocket.h"
#include "evio/AcceptedSocket.h"
#include "evio/EventLoopThread.h"
#include "threadpool/Timer.h"
#include "threadpool/AIThreadPool.h"
#include "utils/AISignals.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

size_t constexpr burst_size = 100000;     // Write this many times 100 bytes.

using evio::MsgBlock;

class MyDecoder : public evio::InputDecoder // Protocol
{
 private:
  size_t m_received;

 public:
  MyDecoder() : m_received(0) { }
  ~MyDecoder() { Dout(dc::notice, "~MyDecoder() [" << this << "]"); }

 protected:
  NAD_DECL(decode, MsgBlock&& msg) override;

  size_t minimum_block_size_estimate() const override { return 4096; }
};

class MyOutputStream4096 : public evio::OutputStream // Protocol
{
 protected:
  size_t minimum_block_size_estimate() const override { return 4096; }
};

using MyAcceptedSocket = evio::AcceptedSocket<MyDecoder, MyOutputStream4096>;

class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
 public:
  using VT_type = evio::ListenSocket<MyAcceptedSocket>::VT_type;

  struct VT_impl : evio::ListenSocket<MyAcceptedSocket>::VT_impl
  {
    // Override
    static void new_connection(ListenSocket* self, accepted_socket_type& accepted_socket)
    {
      Dout(dc::notice, "New connection to listen socket was accepted. Sending " << (100 * burst_size) << " of data.");
      // Write 10 Mbyte of data.
      for (size_t n = 0; n < burst_size; ++n)
        accepted_socket() << "START012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789END.\n";
      accepted_socket() << std::flush;
      accepted_socket.flush_output_device();
      self->close();
    }

    // Virtual table of MyListenSocket.
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
      new_connection            // Overridden
    };
  };

  // Make a deep copy of VT_ptr.
  VT_type* clone_VT() override { return VT_ptr.clone(this); }

  utils::VTPtr<MyListenSocket, evio::ListenSocket<MyAcceptedSocket>> VT_ptr;

  MyListenSocket() : VT_ptr(this) { }
  ~MyListenSocket() { Dout(dc::notice, "~MyListenSocket() [" << this << "]"); }
};

class MyClientSocket : public evio::Socket
{
 public:
  using VT_type = Socket::VT_type;

  struct VT_impl : Socket::VT_impl
  {
    // Override
    static NAD_DECL_UNUSED_ARG(connected, Socket* _self, bool DEBUG_ONLY(success))
    {
      MyClientSocket* self = static_cast<MyClientSocket*>(_self);
      Dout(dc::notice, (success ? "*** CONNECTED ***" : "*** FAILED TO CONNECT ***"));
      ASSERT((self->m_connected_flags & (is_connected|is_disconnected)) == (success ? is_connected : is_disconnected));
      self->m_connected = true;
    }

    static constexpr VT_type VT{
      /*Socket*/
        /*InputDevice*/
      { nullptr,
        read_from_fd,
        hup,
        exceptional,
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

  utils::VTPtr<MyClientSocket, Socket> VT_ptr;

 private:
  bool m_connected;

 public:
  MyClientSocket() : VT_ptr(this), m_connected(false) { }
  ~MyClientSocket() { Dout(dc::notice, "~MyClientSocket() [" << this << "]"); ASSERT(m_connected); }
};

template<threadpool::Timer::time_point::rep count, typename Unit> using Interval = threadpool::Interval<count, Unit>;

TEST(Socket, Constructor)
{
  // Initialize signals. SIGPIPE *must* be ignored or write() won't return EPIPE when peer closed the connection.
  AISignals signals({SIGPIPE});

  // Create a thread pool.
  AIThreadPool thread_pool;
  // Create a new queue with a capacity of 32 and default priority.
  AIQueueHandle queue_handle = thread_pool.new_queue(32);

  try
  {
    // Construct MyDecoder before EventLoop, so that the EventLoop is destructed first!
    // Otherwise the decoder is destructed before we can use it (in the destructor of event_loop).
    //MyDecoder decoder;

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

NAD_DECL(MyDecoder::decode, MsgBlock&& msg)
{
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode(\"" NAD_DoutEntering_ARG << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  m_received += msg.get_size();
  Dout(dc::notice, "m_received = " << m_received);
  // Stop when the last message was received.
  if (m_received == 100 * burst_size)
    NAD_CALL(close_input_device);
}
