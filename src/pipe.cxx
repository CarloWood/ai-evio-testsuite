#include "sys.h"
#include "evio/Pipe.h"
#include "evio/EventLoop.h"
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

 public:
  MyDecoder() : m_received(0) { }

 protected:
  void decode(int& allow_deletion_count, evio::MsgBlock&& msg) override;
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  AIThreadPool thread_pool;
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  evio::OutputStream pipe_source;
  MyDecoder decoder;

  try
  {
    evio::EventLoop event_loop(low_priority_handler);

    // Create a pipe.
    evio::Pipe pipe;
    auto pipe_write_end = pipe.take_write_end();
    auto pipe_read_end = pipe.take_read_end();

    pipe_write_end->set_source(pipe_source);
    pipe_read_end->set_protocol_decoder(decoder);

    pipe_source << "Hello world!" << std::endl;
    pipe_write_end->flush_output_device();

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
