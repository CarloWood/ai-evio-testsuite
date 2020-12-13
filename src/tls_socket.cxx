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
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

class MyDecoder : public evio::protocol::Decoder
{
 private:
  size_t m_received;
  int m_pn;     // Number of matched characters at end of previous chunk.

 public:
  MyDecoder() : m_received(0), m_pn(0) { }

 protected:
  size_t end_of_msg_finder(char const* new_data, size_t rlen) override;
  void decode(int& allow_deletion_count, evio::MsgBlock&& msg) override;
};

class MyOutputStream : public evio::OutputStream
{
  size_t minimum_block_size_estimate() const override { return 100000; }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  evio::SocketAddress endpoint("172.217.20.68:443");
  //evio::SocketAddress endpoint("127.0.0.1:11111");

  AIThreadPool thread_pool;
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  //evio::protocol::TLS::set_debug_level(10);

  MyOutputStream tls_source;
  MyDecoder tls_sink;

  try
  {
    evio::EventLoop event_loop(low_priority_handler);

    auto tls_socket = evio::create<evio::TLSSocket>();

    tls_socket->set_source(tls_source);
    tls_socket->set_sink(tls_sink);
    tls_socket->connect(endpoint, "www.google.com");

    tls_source << "GET /\r\n";
//    tls_source << std::string(40000, 'X');
    tls_source << std::flush;

#if 0
    for (int i = 0; i < 210; ++i)
    {
      tls_source << i << std::endl;
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
#endif

    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}

size_t MyDecoder::end_of_msg_finder(char const* new_data, size_t rlen)
{
  DoutEntering(dc::notice, "MyDecoder::end_of_msg_finder(\"" << buf2str(new_data, rlen) << "\", " << rlen << ")");
  // Find a newline character, or if that doesn't exist, the phrase "</html>".
  char const* newline = static_cast<char const*>(std::memchr(new_data, '\n', rlen));
  if (newline)
    return newline - new_data + 1;
  if ((ssize_t)rlen < 7 - m_pn)
    return 0;
  char const pattern[7] = { '<', '/', 'h', 't', 'm', 'l', '>' };
  if (strncmp(new_data + rlen - (7 - m_pn), pattern + m_pn, 7 - m_pn) == 0)
    return rlen;
  m_pn = 0;
  int n = -1;
  while (++n < 6)
    if (pattern[n] == new_data[rlen - 1])
      break;
  if (++n < 7 && strncmp(new_data - n, pattern, n) == 0)
    m_pn = n;
  return 0;
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
