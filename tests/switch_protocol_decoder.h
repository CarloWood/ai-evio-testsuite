#include "evio/protocol/Decoder.h"
#include "evio/AcceptedSocket.h"
#include "evio/ListenSocket.h"
#include "threadsafe/ConditionVariable.h"
#include "threadsafe/threadsafe.h"
#include "debug.h"
#include <libcwd/buf2str.h>

namespace test_switch_protocol_decoder {

struct Boolean {
  bool value_;
};
using test_finished_type = threadsafe::Unlocked<Boolean, threadsafe::policy::Primitive<threadsafe::ConditionVariable>>;
test_finished_type test_finished_cv;

size_t const large_minimum_block_size = (1 << 14) - evio::block_overhead_c;     // 16352 bytes.

class MyAcceptedSocketSource : public evio::OutputStream
{
 public:
  size_t minimum_block_size() const override { return large_minimum_block_size; }
};

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

using MyAcceptedSocket = evio::AcceptedSocket<MyAcceptedSocketDecoder, MyAcceptedSocketSource>;

class MyListenSocket : public evio::ListenSocket<MyAcceptedSocket>
{
 protected:
  // Called when a new connection is accepted.
  void new_connection(accepted_socket_type& accepted_socket) override
  {
    Dout(dc::notice, "New connection to listen socket was accepted. Sending 16300 bytes of data.");
    // Write 10 kbyte of data.
    for (int n = 0; n < large_minimum_block_size / 100; ++n)
      accepted_socket() << "START012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789END.\n";     // Write to output buffer.

    // Write 10 bytes of the next message (incomplete).
    accepted_socket() << "START01234";

    // We're done with writing to this socket.
    accepted_socket() << std::flush;            // Start writing to socket.

    // Horrible hack to get the above to be already sent over the socket,
    // before we write the next message to that socket.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Write one more 90 byte message.
    accepted_socket() << "5678901234567890123456789012345678901234567890123456789012345678901234567890123456789END." << std::endl;;
    accepted_socket.flush_output_device();      // Close output device as soon as buffer is empty.
  }
};

class MyYDecoder : public evio::protocol::Decoder
{
 private:
  size_t minimum_block_size() const override { return 2016; }

 private:
  int m_received;

 public:
  MyYDecoder() : m_received(0) { }

 public:
  void decode(int& allow_deletion_count, evio::MsgBlock&& CWDEBUG_ONLY(msg)) override;
};

class MyXDecoder : public evio::protocol::Decoder
{
 private:
  int m_received;
  MyYDecoder m_ydecoder;

 public:
  MyXDecoder() : m_received(0) { }

 public:
  size_t minimum_block_size() const override { return large_minimum_block_size; }
  void decode(int& allow_deletion_count, evio::MsgBlock&& CWDEBUG_ONLY(msg)) override;
};

class MySocket : public evio::Socket
{
 private:
  MyXDecoder m_xdecoder;
  evio::OutputStream m_output_stream;

 public:
  MySocket()
  {
    DoutEntering(dc::notice, "MySocket::MySocket()");
    set_protocol_decoder(m_xdecoder);
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

  // Switch protocol after receiving the first 16300 bytes.
  if (m_received == 16300)
    switch_protocol_decoder(m_ydecoder, 8000, 16000);
}

void MyYDecoder::decode(int& allow_deletion_count, evio::MsgBlock&& CWDEBUG_ONLY(msg))
{
  DoutEntering(dc::notice, "MyYDecoder::decode({" << allow_deletion_count << "}, \"" << libcwd::buf2str(msg.get_start(), msg.get_size()) << "\")");

  m_received += msg.get_size();
  Dout(dc::notice, "Received " << m_received << " bytes in total.");

  // Close the socket as soon as 100 bytes were received.
  if (m_received == 100)
  {
    close_input_device(allow_deletion_count);
    test_finished_type::wat test_finished_w(test_finished_cv);
    test_finished_w->value_ = true;
    test_finished_w.notify_one();

    MySocket* socket = static_cast<MySocket*>(m_input_device);
    socket->finished();
  }
}

} // namespace test_switch_protocol_decoder

#include "EventLoopFixture.h"

using change_specsFixture = EventLoopFixture<testing::Test>;

TEST_F(change_specsFixture, change_specs)
{
  using namespace test_switch_protocol_decoder;

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
    test_finished_w.wait([&](){ return test_finished_w->value_; });
  }
  Dout(dc::notice, "test_finished_cv became true.");

  // Close the listen socket.
  listen_sock->close();

  EXPECT_TRUE(true);
}
