// Run this test while './http_server' from the project https://github.com/CarloWood/curl_http_pipeline_tester.git is already running.

#include "sys.h"
#include "debug.h"
#include "threadpool/AIThreadPool.h"
#include "evio/EventLoopThread.h"
#include "evio/InputDecoder.h"
#include "evio/Socket.h"
#include "evio/OutputStream.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

using evio::InputDecoder;
using evio::MsgBlock;
using evio::RefCountReleaser;
using evio::Socket;
using evio::OutputStream;
using evio::SocketAddress;
using evio::GetThread;

class InputPrinter : public InputDecoder
{
 protected:
  RefCountReleaser decode(MsgBlock&& msg, GetThread) override;
};

class MySocket : public Socket
{
 private:
  InputPrinter m_input_printer;

 public:
  MySocket() { input(m_input_printer); }
};

using debug::Mark;

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  // Create a thread pool of 2 threads.
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
    Mark m1;
    OutputStream output_stream[number_of_sockets];
    m1.end();
    for (int i = 0; i < number_of_sockets; ++i)
    {
      {
        Dout(dc::notice, "Creating MySocket object #" << i);
        Mark m2;
        socket[i] = new MySocket;
      }
      {
        Dout(dc::notice, "Linking OutputStream #" << i << " to socket #" << i);
        Mark m3;
        socket[i]->output(output_stream[i]);
      }
    }
    for (int i = 0; i < number_of_sockets; ++i)
    {
      Dout(dc::notice, "Calling socket[" << i << "]->connect()");
      Mark m4;
      socket[i]->connect(SocketAddress("127.0.0.1", 9001));
    }

    // Write 6 requests to each socket.
    for (int request = 0; request < 6; ++request)
      for (int i = 0; i < number_of_sockets; ++i)
      {
        Dout(dc::notice, "Writing data to output_stream[" << i << "]");
        Mark m5;
        output_stream[i] << "GET / HTTP/1.1\r\n"
                            "Host: localhost:9001\r\n"
                            "Accept: */*\r\n"
                            "X-Request: " << request << "\r\n"
                            "X-Sleep: " << (200 * request) << "\r\n"
                            "\r\n" << std::flush;
      }
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }

  event_loop.join();
}

RefCountReleaser InputPrinter::decode(MsgBlock&& msg, GetThread)
{
  RefCountReleaser need_allow_deletion;
  // Just print what was received.
  DoutEntering(dc::notice, "InputPrinter::decode(\"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  // Stop when the last message was received.
  if (msg.get_size() >= 17 && strncmp(msg.get_start() + msg.get_size() - 17, "#5</body></html>\n", 17) == 0)
    need_allow_deletion = stop_input_device();
  return need_allow_deletion;
}
