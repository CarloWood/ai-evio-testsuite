#include "sys.h"
#include "evio/EventLoop.h"
#include "evio/Socket.h"
#include "evio/protocol/TLS.h"
#include "evio/Source.h"
#include "evio/Sink.h"
#include "evio/TLSSocket.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#include "debug.h"
#include <libcwd/buf2str.h>

class MyDecoder : public evio::InputDecoder
{
 private:
  size_t m_received;

 public:
  MyDecoder() : m_received(0) { }

 protected:
  void decode(int& allow_deletion_count, evio::MsgBlock&& msg) override;
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  evio::SocketAddress endpoint("127.0.0.1:5556");

  AIThreadPool thread_pool;
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  //evio::protocol::TLS::set_debug_level(10);

  evio::OutputStream tls_source;
  MyDecoder tls_sink;

  try
  {
    evio::EventLoop event_loop(low_priority_handler);

    auto tls_socket = evio::create<evio::TLSSocket>();

    tls_socket->set_source(tls_source);
    tls_socket->set_sink(tls_sink);
    tls_socket->connect(endpoint);

    tls_source << "Hello world!" << std::endl;
    //tls_socket->flush_output_device();

    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}

void MyDecoder::decode(int& CWDEBUG_ONLY(allow_deletion_count), evio::MsgBlock&& CWDEBUG_ONLY(msg))
{
  // Just print what was received.
  DoutEntering(dc::notice, "MyDecoder::decode({" << allow_deletion_count << "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  Debug(m_received += msg.get_size());
  if (std::string_view(msg.get_start(), msg.get_size()) == "Hello world!\n")
  {
    close_input_device(allow_deletion_count);
  }
}
