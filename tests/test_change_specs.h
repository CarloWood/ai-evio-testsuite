#include "evio/protocol/Decoder.h"
#include "evio/AcceptedSocket.h"
#include "evio/ListenSocket.h"
#include "threadsafe/ConditionVariable.h"
#include "threadsafe/aithreadsafe.h"
#include "debug.h"
#include <libcwd/buf2str.h>

using test_finished_type = aithreadsafe::Wrapper<bool, aithreadsafe::policy::Primitive<aithreadsafe::ConditionVariable>>;
test_finished_type test_finished_cv;

class MyAcceptedSocketDecoder : public evio::protocol::Decoder
{
 protected:
  void decode(int& allow_deletion_count, evio::MsgBlock&& msg) override;
};

void MyAcceptedSocketDecoder::decode(int& allow_deletion_count, evio::MsgBlock&& msg)
{
  // Just print what was received.
  DoutEntering(dc::notice, "MyAcceptedSocketDecoder::decode({" << allow_deletion_count << "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
  close_input_device(allow_deletion_count);
}

using MyAcceptedSocket = evio::AcceptedSocket<MyAcceptedSocketDecoder, evio::OutputStream>;

class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
 protected:
  // Called when a new connection is accepted.
  void new_connection(accepted_socket_type& accepted_socket) override
  {
    Dout(dc::notice, "New connection to listen socket was accepted. Sending 10000 bytes of data.");
    // Write 10 kbyte of data.
    for (int n = 0; n < 100; ++n)
      accepted_socket() << "START012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789END.\n";     // Write to output buffer.
    // We're done with writing to this socket.
    accepted_socket() << std::flush;            // Start writing to socket.
    accepted_socket.flush_output_device();      // Close output device as soon as buffer is empty.
  }
};

class MyXDecoder : public evio::protocol::Decoder
{
 private:
  size_t minimum_block_size() const override { return 100000; }

 private:
  int m_received;

 public:
  MyXDecoder() : m_received(0) { }

 public:
  void decode(int& allow_deletion_count, evio::MsgBlock&& CWDEBUG_ONLY(msg)) override;
};

class MySocket : public evio::Socket
{
 private:
  MyXDecoder m_decoder;
  evio::OutputStream m_output_stream;

 public:
  MySocket()
  {
    DoutEntering(dc::notice, "MySocket::MySocket()");
    set_protocol_decoder(m_decoder);
    set_source(m_output_stream);
    //on_connected([this](int& allow_deletion_count, bool success){ connected(allow_deletion_count, success); });
  }

  void connected(int& allow_deletion_count, bool DEBUG_ONLY(success))
  {
    Dout(dc::notice, (success ? "*** CONNECTED ***" : "*** FAILED TO CONNECT ***"));
    ASSERT((m_connected_flags & (is_connected|is_disconnected)) == (success ? is_connected : is_disconnected));
  }

  void finished()
  {
    m_output_stream << "Done" << std::endl;
    flush_output_device();
  }
};

void MyXDecoder::decode(int& allow_deletion_count, evio::MsgBlock&& CWDEBUG_ONLY(msg))
{
  DoutEntering(dc::notice, "MyXDecoder::decode({" << allow_deletion_count << "}, \"" << libcwd::buf2str(msg.get_start(), msg.get_size()) << "\")");

  m_received += msg.get_size();
  Dout(dc::notice, "Received " << m_received << " bytes in total.");

  // Close the socket as soon as 10000 bytes were received.
  if (m_received == 10000)
  {
    close_input_device(allow_deletion_count);
    test_finished_type::wat test_finished_w(test_finished_cv);
    *test_finished_w = true;
    test_finished_w.notify_one();

    MySocket* socket = static_cast<MySocket*>(m_input_device);
    socket->finished();
  }
}

#include "EventLoopFixture.h"

using change_specsFixture = EventLoopFixture<testing::Test>;

TEST_F(change_specsFixture, change_specs)
{
  // Create a listen socket.
  static evio::SocketAddress const listen_address("127.0.0.1:9002");

  // Start a listen socket on port 9001.
  auto listen_sock = evio::create<MyListenSocket>();
  listen_sock->listen(listen_address);

  // Create a socket and connect it to the listen socket.
  auto socket = evio::create<MySocket>();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));          // Dumb way to wait until the listen socket is up.
  socket->connect(listen_address);

  Dout(dc::notice, "Waiting for test_finished_cv to become true.");
  {
    test_finished_type::wat test_finished_w(test_finished_cv);
    test_finished_w.wait([&](){ return *test_finished_w; });
  }
  Dout(dc::notice, "test_finished_cv became true.");

  // Close the listen socket.
  listen_sock->close();

  EXPECT_TRUE(true);
}
