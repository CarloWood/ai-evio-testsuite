#include "sys.h"
#include "evio/EventLoop.h"
#include "evio/AcceptedSocket.h"
#include "evio/ListenSocket.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#include "debug.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

class MyDecoder : public evio::InputDecoder
{
 private:
  size_t m_received;

 public:
  MyDecoder() : m_received(0) { }

 protected:
  void decode(int& allow_deletion_count, evio::MsgBlock&& msg) override;
};

using MyAcceptedSocket = evio::AcceptedSocket<MyDecoder, evio::OutputStream>;

class MyUNIXListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
  void new_connection(accepted_socket_type& UNUSED_ARG(accepted_socket)) override
  {
    close();
  }
};

class MyUNIXSocket : public evio::Socket
{
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  evio::SocketAddress endpoint("/tmp/unix_socket");

  AIThreadPool thread_pool;
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  evio::OutputStream unix_socket_source;

  try
  {
    evio::EventLoop event_loop(low_priority_handler);

    // Create a listen socket.
    auto listen_socket = evio::create<MyUNIXListenSocket>();
    listen_socket->listen(endpoint);

    auto unix_socket = evio::create<MyUNIXSocket>();
    unix_socket->set_source(unix_socket_source);
    unix_socket->connect(endpoint);

    unix_socket_source << "Hello world!" << std::endl;
    unix_socket->close_output_device();

    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}

void MyDecoder::decode(int& allow_deletion_count, evio::MsgBlock&& msg)
{
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode({" << allow_deletion_count << "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  Debug(m_received += msg.get_size());
  if (std::string_view(msg.get_start(), msg.get_size()) == "Hello world!\n")
  {
    close_input_device(allow_deletion_count);
  }
}
