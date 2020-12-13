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

class MyAcceptedSocketDecoder : public Decoder
{
 private:
  size_t m_received;

 public:
  MyAcceptedSocketDecoder() : m_received(0) { }

 protected:
  size_t end_of_msg_finder(char const* new_data, size_t rlen) override { return rlen; }
  void decode(int& allow_deletion_count, MsgBlock&& msg) override;
};

using MyAcceptedSocket = evio::AcceptedSocket<MyAcceptedSocketDecoder, OutputStream>;

class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
 public:
  // Called when a new connection is accepted.
  void new_connection(accepted_socket_type& accepted_socket) override
  {
    Dout(dc::notice, "New connection to listen socket was accepted. Sending 10000 bytes of data.");
    // Write 10 kbyte of data.
    for (int n = 0; n < 100; ++n)
      accepted_socket() << "START012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789END." << std::endl;
    // We're done with writing to this socket.
    accepted_socket.flush_output_device();
  }
};

class MyDecoder : public Decoder
{
 private:
  size_t m_received;

 public:
  MyDecoder() : m_received(0) { }

 protected:
  void decode(int& allow_deletion_count, MsgBlock&& msg) override;
};

class BurstSocket : public evio::Socket
{
  private:
   MyDecoder m_input;
   OutputStream m_output;

  public:
   BurstSocket()
   {
     set_sink(m_input);
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
    // This timer fires while we're in the destructor of event_loop.
    // Therefore it must be constructed before event_loop, or it would
    // be cancelled before it expires.
    threadpool::Timer timer;

    // Initialize the IO event loop thread.
    evio::EventLoop event_loop(low_priority_handler);

    // Start a listen socket on port 9001 that is closed after 10 seconds.
    {
      auto listen_sock = evio::create<MyListenSocket>();
      listen_sock->listen(listen_address);
      timer.start(Interval<10, std::chrono::seconds>(),
          [&timer, listen_sock]()
          {
            listen_sock->close();
            timer.release_callback();   // This destroys `listen_sock` (the copy inside this lambda).
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
        sockets[s]->flush_output_device();
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

void MyAcceptedSocketDecoder::decode(int& allow_deletion_count, MsgBlock&& msg)
{
  // Just print what was received.
  DoutEntering(dc::notice, "MyAcceptedDecoder::decode({" << allow_deletion_count << "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  m_received += msg.get_size();
  // Close the socket when the last message was received.
  if (m_received == 22) // Two times "Burst data!".
    close_input_device(allow_deletion_count);
}

void MyDecoder::decode(int& allow_deletion_count, MsgBlock&& msg)
{
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode({" << allow_deletion_count << "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  m_received += msg.get_size();
  // Stop when the last message was received.
  if (m_received == 10000)
    close_input_device(allow_deletion_count);
}
