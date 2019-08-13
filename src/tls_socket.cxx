#include "sys.h"
#include "evio/EventLoop.h"
#include "evio/Socket.h"
#include "evio/protocol/TLS.h"
#include "evio/Source.h"
#include "evio/Sink.h"
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

class TLSSession : public evio::Socket
{
 private:
  evio::protocol::TLS m_tls;

 public:
  void set_source(evio::Source const&)
  {
  }

  void set_sink(evio::Sink const&)
  {
  }

  bool connect(evio::SocketAddress const& remote_address, size_t rcvbuf_size = 0, size_t sndbuf_size = 0, evio::SocketAddress if_addr = {})
  {
    m_tls.set_device(this, this);
    int ret = evio::Socket::connect(remote_address, rcvbuf_size, sndbuf_size, if_addr);
    m_tls.session_init(remote_address);
    return ret;
  }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  evio::SocketAddress endpoint("127.0.0.1:4433");

  AIThreadPool thread_pool;
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  //evio::protocol::TLS::set_debug_level(10);

  evio::OutputStream tls_source;
  MyDecoder tls_sink;

  try
  {
    evio::EventLoop event_loop(low_priority_handler);

    auto tls_socket = evio::create<TLSSession>();

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
