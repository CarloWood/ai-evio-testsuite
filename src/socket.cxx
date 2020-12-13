// Run this test while './http_server' from the project https://github.com/CarloWood/curl_http_pipeline_tester.git is already running.

#include "sys.h"
#include "debug.h"
#include "evio/EventLoop.h"
#include "evio/Socket.h"
#include "evio/OutputStream.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

using evio::protocol::Decoder;
using evio::MsgBlock;
using evio::Socket;
using evio::OutputStream;
using evio::SocketAddress;
using evio::GetThread;

class InputPrinter : public Decoder
{
 protected:
  void decode(int& allow_deletion_count, MsgBlock&& msg) override;
};

class MySocket : public Socket
{
 private:
  InputPrinter m_decoder;

 public:
  MySocket() { DoutEntering(dc::notice, "MySocket::MySocket()"); set_protocol_decoder(m_decoder); }
};

#ifdef CWDEBUG
using debug::Mark;
#endif

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  // Create a thread pool.
  AIThreadPool thread_pool;
  [[maybe_unused]] AIQueueHandle high_priority_handler = thread_pool.new_queue(32);
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  // Initialize the IO event loop thread.
  evio::EventLoop event_loop(low_priority_handler);

  try
  {
    constexpr int number_of_sockets = 4;
    boost::intrusive_ptr<MySocket> socket[number_of_sockets];
    Dout(dc::notice, "Creating OutputStream objects.");
#ifdef CWDEBUG
    Mark m1;
#endif
    OutputStream output_stream[number_of_sockets];
#ifdef CWDEBUG
    m1.end();
#endif
    for (int i = 0; i < number_of_sockets; ++i)
    {
      {
        Dout(dc::notice, "Creating MySocket object #" << i);
#ifdef CWDEBUG
        Mark m2;
#endif
        socket[i] = new MySocket;
      }
      {
        Dout(dc::notice, "Linking OutputStream #" << i << " to socket #" << i);
#ifdef CWDEBUG
        Mark m3;
#endif
        socket[i]->set_source(output_stream[i]);
      }
    }
    for (int i = 0; i < number_of_sockets; ++i)
    {
      Dout(dc::notice, "Calling socket[" << i << "]->connect()");
#ifdef CWDEBUG
      Mark m4;
#endif
      socket[i]->connect(SocketAddress("127.0.0.1", 9001));
    }

    // Write 6 requests to each socket.
    for (int request = 0; request < 6; ++request)
      for (int i = 0; i < number_of_sockets; ++i)
      {
        Dout(dc::notice, "Writing data to output_stream[" << i << "]");
#ifdef CWDEBUG
        Mark m5;
#endif
        output_stream[i] << "GET / HTTP/1.1\r\n"
                            "Host: localhost:9001\r\n"
                            "Accept: */*\r\n"
                            "X-Request: " << request << "\r\n"
                            "X-Sleep: " << (200 * request) << "\r\n"
                            "\r\n" << std::flush;
      }

    // Signal that we're done writing to the sockets.
    for (int i = 0; i < number_of_sockets; ++i)
      socket[i]->flush_output_device();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }

  event_loop.join();
}

void InputPrinter::decode(int& allow_deletion_count, MsgBlock&& msg)
{
  // Just print what was received.
  DoutEntering(dc::notice, "InputPrinter::decode({" << allow_deletion_count << "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  // Stop when the last message was received.
  if (msg.get_size() >= 17 && strncmp(msg.get_start() + msg.get_size() - 17, "#5</body></html>\n", 17) == 0)
    // This was the last message that we're interested in receiving (and probably nothing will come after this anyway).
    close_input_device(allow_deletion_count);
}
