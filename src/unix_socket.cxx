#include "sys.h"                                // This will pick up cwds/sys.h, unless overridden by a sys.h in the root of the project.
#include "evio/EventLoop.h"
#include "evio/AcceptedSocket.h"
#include "evio/ListenSocket.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#include "debug.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

// Decoder for the accepted socket.
class MyDecoder : public evio::protocol::Decoder
{
 protected:
  // Call decode() with chunks ending on a newline (the default).
  void decode(int& allow_deletion_count, evio::MsgBlock&& msg) override
  {
    // Just print what was received.
    DoutEntering(dc::notice, "MyDecoder::decode({" << allow_deletion_count <<
        "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');

    // Close this connection as soon as we received "Hello world!\n".
    if (std::string_view(msg.get_start(), msg.get_size()) == "Hello world!\n")
      close_input_device(allow_deletion_count);
  }
};

// The type of the accepted socket uses MyDecoder as decoder.
using MyAcceptedSocket = evio::AcceptedSocket<MyDecoder, evio::OutputStream>;

// The type of my listen socket.
class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
  // As soon as the first connection is received, close this listen socket.
  void new_connection(accepted_socket_type& UNUSED_ARG(accepted_socket)) override
  {
    close();
  }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  // This tells the rest of the code to use a UNIX socket,
  // you can also use an IP# and port here, of course.
  evio::SocketAddress endpoint("/tmp/unix_socket");

  // Fire up the thread pool.
  AIThreadPool thread_pool;
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  // An object that allows as to write to an ostream
  // in order to write to (the buffer of) the socket.
  evio::OutputStream socket_stream;

  try
  {
    evio::EventLoop event_loop(low_priority_handler);

    // Create a listen socket.
    auto listen_socket = evio::create<MyListenSocket>();
    listen_socket->listen(endpoint);

    // Create a UNIX socket and connect it to our listen socket.
    auto socket = evio::create<evio::Socket>();
    // Write data to the output buffer of the socket.
    socket->set_source(socket_stream);
    socket_stream << "Hello world!" << std::endl;
    // Connect it to the end point.
    socket->connect(endpoint);
    // Because a UNIX socket connect succeeds immediately, it is safe
    // to call flush_output_device already. For a normal socket you'd
    // have to do with in the callback of on_connected.
    //
    // This will cause the socket to be closed as soon as
    // the whole buffer was written to the socket.
    socket->flush_output_device();

    // Wind down.
    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}

