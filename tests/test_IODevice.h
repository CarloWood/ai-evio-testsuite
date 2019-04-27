#include "evio/InputDevice.h"
#include "evio/SocketAddress.h"
#include "evio/inet_support.h"
#include "libcwd/buf2str.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>

// Work around the fact that InputDevice and OutputDevice have a protected constructor and protected methods.

struct InputDevice : public evio::InputDevice
{
  using evio::InputDevice::InputDevice;

  int get_input_fd() const override
  {
    return evio::InputDevice::get_input_fd();
  }

  void start_input_device(evio::GetThread type)
  {
    evio::InputDevice::start_input_device(type);
  }

  evio::RefCountReleaser stop_input_device()
  {
    return evio::InputDevice::stop_input_device();
  }

  void disable_input_device()
  {
    evio::InputDevice::disable_input_device();
  }

  void enable_input_device(evio::GetThread type)
  {
    evio::InputDevice::enable_input_device(type);
  }
};

struct OutputDevice : public evio::OutputDevice
{
  using evio::OutputDevice::OutputDevice;

  int get_output_fd() const override
  {
    return evio::OutputDevice::get_output_fd();
  }

  void start_output_device(evio::PutThread type)
  {
    evio::OutputDevice::start_output_device(type);
  }

  evio::RefCountReleaser stop_output_device()
  {
    return evio::OutputDevice::stop_output_device();
  }

  void disable_output_device()
  {
    evio::OutputDevice::disable_output_device();
  }

  void enable_output_device(evio::PutThread type)
  {
    evio::OutputDevice::enable_output_device(type);
  }
};

// Test Fixture

class TestIODeviceNoInit : public testing::Test
{
 protected:
  int pipefd[2];
  boost::intrusive_ptr<InputDevice> m_input_device;
  boost::intrusive_ptr<OutputDevice> m_output_device;

 protected:
  void SetUp() override
  {
    DoutEntering(dc::notice, "TestIODeviceNoInit::SetUp()");
    int res = pipe2(pipefd, O_DIRECT | O_CLOEXEC);
    if (res == -1)
      perror("pipe2");
    ASSERT(res == 0);
    m_input_device = evio::create<InputDevice>();
    m_output_device = evio::create<OutputDevice>();
  }

  void TearDown() override
  {
    DoutEntering(dc::notice, "TestIODeviceNoInit::TearDown()");
    // Delete the device objects.
    m_input_device.reset();
    m_output_device.reset();
    // Clean up the pipe.
    if (evio::is_valid(pipefd[0]))
      close(pipefd[0]);
    if (evio::is_valid(pipefd[1]))
      close(pipefd[1]);
  }
};

class TestIODevice : public TestIODeviceNoInit
{
 protected:
  void SetUp() override
  {
    DoutEntering(dc::notice, "TestIODevice::SetUp()");
    TestIODeviceNoInit::SetUp();
    m_input_device->init(pipefd[0]);
    m_output_device->init(pipefd[1]);
  }

  void TearDown() override
  {
    DoutEntering(dc::notice, "TestIODevice::TearDown()");
    if (m_input_device->is_active(evio::SingleThread()).is_momentary_true())
    {
      auto need_allow_deletion = m_input_device->stop_input_device();
      // is_momentary_true() here really means is_true() -- because we don't have an EventLoopThread running during this test, nor any other threads.
      // Therefore, this should always be true.
      EXPECT_TRUE(need_allow_deletion); // To cancel inhibit_deletion of being active.
    }
    if (m_output_device->is_active(evio::SingleThread()).is_momentary_true())
    {
      auto need_allow_deletion = m_output_device->stop_output_device();
      // See comment above.
      EXPECT_TRUE(need_allow_deletion); // To cancel inhibit_deletion of being active.
    }
    // Make sure these are really the last references.
    EXPECT_TRUE(m_input_device->unique().is_true());
    EXPECT_TRUE(m_output_device->unique().is_true());
    m_input_device.reset();             // Release last reference to device; this should close it.
    m_output_device.reset();            // Release last reference to device; this should close it.
    // By now both file descriptors should be closed.
    EXPECT_FALSE(evio::is_valid(pipefd[0]));
    EXPECT_FALSE(evio::is_valid(pipefd[1]));
  }
};

TEST_F(TestIODeviceNoInit, SetUpAndTearDown) {
  EXPECT_EQ(m_input_device->get_input_fd(), -1);
  EXPECT_EQ(m_output_device->get_output_fd(), -1);
}

TEST_F(TestIODevice, SetUpAndTearDown) {
  EXPECT_EQ(m_input_device->get_input_fd(), pipefd[0]);
  EXPECT_EQ(m_output_device->get_output_fd(), pipefd[1]);
}

TEST_F(TestIODevice, StartStop) {
  // Introduction
  // ------------
  //
  // Because certain operations are only (allowed to be) done by a certain type of thread,
  // of which only one thread at a time may be accessing the library, being such a thread
  // guarantees that those operations are not done concurrently, without the need to lock
  // a mutex.
  //
  // More specifically, this design has been implemented to deal with writing to device
  // (buffer)s, and reading from device (buffer)s. It is obviously undesirable to have
  // more than one thread at the same time write to a device (buffer), or read from one.
  // This is not a matter of locking a mutex to make the access to the buffer thread-safe;
  // while that would stop Undefined Behavior (and thus theoretical crashes) you'd end
  // up with two interleaving streams of characters. Hence, such is not done and the library
  // may assume that it is not done.
  //
  // Input- and Output- Devices are modelled as follows:
  //
  //   The PutThread      .--------.     The EventLoopThread reads
  //    write to the  ==> | buffer | ==> reads from the buffer and   ==> OutputDevice
  //   output buffer      `--------'     writes to the OutPutDevice
  //
  //   The GetThread      .--------.     The EventLoopThread reads
  //   reads from the <== | buffer | <== from the InputDevice and    <== InputDevice
  //   input buffer       `--------'     writes to the buffer
  //
  // where 'PutThread' may be multiple threads, but never more than one at a time and
  // therefore can be safely considered to be a single thread if you want. Likewise
  // the 'GetThread' may be multiple threads, but never more than one at a time. We
  // will therefore exclusively speak about The PutThread and The GetThread, but keep
  // in mind that under certain circumstances a thread can be both at the same time
  // (meaning that it is guaranteed that neither type of operations are done concurrently).
  //
  // The EventLoopThread is really a separate thread that is dedicated to its task.
  //
  // Both, InputDevice and OutputDevice represent a file descriptor (possibly the same
  // one). When libev is asked to monitor a file descriptor for read- or writablity,
  // it is said that the underlaying ev_io 'watcher' struct is made 'active'.
  // The evio library speaks about starting a device to make it active, and stopping it
  // to make it inactive.
  //
  // Starting and stopping a device
  // ------------------------------
  //
  // An OutputDevice is started exclusively by the PutThread, usually after actually
  // writing some data to the buffer.
  //
  // An OutputDevice is normally stopped by the EventLoopThread when the buffer runs
  // empty, but it can also be stopped by any other thread - either by a call to
  // disable_output_device() or close_output_device().
  //
  // An InputDevice is started upon initialization of the device and only stopped
  // by the EventLoopThread when the buffer is full, but can also be stopped, by
  // any thread, by a call to disable_input_device() or close_input_device().
  //
  // Restarting of an InputDevice therefore happens by the GetThread, when that
  // read something from a full buffer -- or by a call to enable_input_device().
  // By restricting the latter to the GetThread (aka, you may only enable a disabled
  // input device while you're not trying to read from it; makes sense) we can
  // guarantee that an InputDevice is started exclusively by the GetThread, either
  // after reading something from a full buffer, or by re-enabling an InputDevice.
  //
  // The important parts of the above is where a transition is exclusive to either
  // the PutThread or the GetThread. So we have:
  //
  // OutputDevice
  //
  //   active --> inactive  (EventLoopThread (buffer empty), AnyThread (disable_output_device / close_output_device)).
  //   inactive --> active  (PutThread)
  //
  // InputDevice
  //
  //   active --> inactive  (EventLoopThread (buffer full), AnyThread (disable_input_device / close_input_device)).
  //   inactive --> active  (GetThread)
  //
  // This means that is_active() must always return WasFalse or WasTrue (because it
  // could change at any moment) unless we are the PutThread and the device is an
  // inactive OutputDevice, or when we are the GetThread and the device is an
  // inactive InputDevice.
  //
  // Note that the type evio::SingleThread just means "both PutThread and GetThread".
  // I'd be totally willing to have that exclude AnyThread (any other thread), but
  // it just can't exclude actions by the EventLoopThread -- so it makes little sense
  // to define it otherwise.

  evio::SingleThread type;

  // Before starting:
  //
  // Inactive InputDevice.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_input_device->is_active(evio::GetThread()).is_false());
  EXPECT_TRUE(m_input_device->is_active(evio::PutThread()).is_transitory_false());
  EXPECT_TRUE(m_input_device->is_active(evio::AnyThread()).is_transitory_false());
  // Inactive OutputDevice.
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::GetThread()).is_transitory_false());
  EXPECT_TRUE(m_output_device->is_active(evio::PutThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::AnyThread()).is_transitory_false());

  // m_input_device and m_output_device respectively are the only pointers to the device objects.
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());

  m_input_device->start_input_device(type);
  m_output_device->start_output_device(type);

  // Starting a device causes a call to inhibit_deletion().
  EXPECT_TRUE(m_input_device->unique().is_momentary_false());
  EXPECT_TRUE(m_output_device->unique().is_momentary_false());

  // After starting:
  //
  // Active InputDevice
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_transitory_true());
  EXPECT_TRUE(m_input_device->is_active(evio::GetThread()).is_transitory_true());
  EXPECT_TRUE(m_input_device->is_active(evio::PutThread()).is_transitory_true());
  EXPECT_TRUE(m_input_device->is_active(evio::AnyThread()).is_transitory_true());
  // Active OutputDevice.
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_transitory_true());
  EXPECT_TRUE(m_output_device->is_active(evio::GetThread()).is_transitory_true());
  EXPECT_TRUE(m_output_device->is_active(evio::PutThread()).is_transitory_true());
  EXPECT_TRUE(m_output_device->is_active(evio::AnyThread()).is_transitory_true());

  m_input_device->stop_input_device();
  m_output_device->stop_output_device();

  // After stopping.
  //
  // Inactive InputDevice.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_input_device->is_active(evio::GetThread()).is_false());
  EXPECT_TRUE(m_input_device->is_active(evio::PutThread()).is_transitory_false());
  EXPECT_TRUE(m_input_device->is_active(evio::AnyThread()).is_transitory_false());
  // Inactive OutputDevice.
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::GetThread()).is_transitory_false());
  EXPECT_TRUE(m_output_device->is_active(evio::PutThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::AnyThread()).is_transitory_false());

  // Now that the devices are inactive again, the inhibit was removed.
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());
}

TEST_F(TestIODevice, DisableStartEnableStartStop) {
  evio::SingleThread type;

  // If a device is disabled before starting...
  m_input_device->disable_input_device();
  m_output_device->disable_output_device();
  // Then the devices are not kept alive.
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());

  // Starting while being disabled prints a WARNING (you shouldn't do that).
  m_input_device->start_input_device(type);
  m_output_device->start_output_device(type);
  // And they weren't started.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_false());
  // Nor was inhibit_deletion() called therefore.
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());

  // Enabling the devices again...
  m_input_device->enable_input_device(type);
  m_output_device->enable_output_device(type);
  // DOES start them however.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_transitory_true());
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_transitory_true());
  // Which also inhibits deletion.
  EXPECT_TRUE(m_input_device->unique().is_momentary_false());
  EXPECT_TRUE(m_output_device->unique().is_momentary_false());

  // Starting devices that are already started does nothing.
  m_input_device->start_input_device(type);
  m_output_device->start_output_device(type);

  // Stopping them...
  m_input_device->stop_input_device();
  m_output_device->stop_output_device();
  // Makes them inactive again.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_false());
  // And the inhibit_deletion was cancelled.
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());
}

TEST_F(TestIODevice, StartDisableEnableClose) {
  evio::SingleThread type;
  // Disabling devices that are already started...
  m_input_device->start_input_device(type);
  m_output_device->start_output_device(type);
  m_input_device->disable_input_device();
  m_output_device->disable_output_device();
  // Should still keep them alive.
  EXPECT_TRUE(m_input_device->unique().is_momentary_false());
  EXPECT_TRUE(m_output_device->unique().is_momentary_false());
  // Despite that they aren't active.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_false());

  m_input_device->enable_input_device(type);
  m_output_device->enable_output_device(type);
  m_input_device->close_input_device();
  m_output_device->close_output_device();
}

TEST_F(TestIODevice, StartDisableClose) {
  evio::SingleThread type;
  m_input_device->start_input_device(type);
  m_output_device->start_output_device(type);
  m_input_device->disable_input_device();
  m_output_device->disable_output_device();
  m_input_device->close_input_device();
  m_output_device->close_output_device();
}

TEST_F(TestIODevice, StartDisableEnableStop) {
  evio::SingleThread type;
  m_input_device->start_input_device(type);
  m_output_device->start_output_device(type);
  m_input_device->disable_input_device();
  m_output_device->disable_output_device();
  m_input_device->stop_input_device();
  m_output_device->stop_output_device();
  EXPECT_TRUE(m_input_device->unique().is_momentary_false());
  EXPECT_TRUE(m_output_device->unique().is_momentary_false());
  m_input_device->close_input_device();
  m_output_device->close_output_device();
}

// A class to test InputDevice and OutputDevice.
class TestSocket : public evio::InputDevice, public evio::OutputDevice
{
 private:
  int m_socket_fd;
  int m_write_to_fd_count;
  int m_read_from_fd_count;
  int m_read_returned_zero_count;
  int m_read_error_count;
  int m_data_received_count;
  int m_write_error_count;

 public:
  struct VT_type : evio::InputDevice::VT_type, evio::OutputDevice::VT_type
  {
  };

  struct VT_impl : evio::InputDevice::VT_impl, evio::OutputDevice::VT_impl
  {
    static void read_from_fd(evio::InputDevice* _self, int fd)
    {
      DoutEntering(dc::notice|flush_cf, "TestSocket::read_from_fd(" << (void*)_self << ", " << fd << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_read_from_fd_count++;
      evio::InputDevice::VT_impl::read_from_fd(_self, fd);
    }
    static evio::RefCountReleaser read_returned_zero(evio::InputDevice* _self)
    {
      DoutEntering(dc::notice|flush_cf, "TestSocket::read_returned_zero(" << (void*)_self << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_read_returned_zero_count++;
      return evio::InputDevice::VT_impl::read_returned_zero(_self);
    }
    static evio::RefCountReleaser read_error(evio::InputDevice* _self, int err)
    {
      DoutEntering(dc::notice|flush_cf, "TestSocket::read_error(" << (void*)_self << ", " << err << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_read_error_count++;
      return evio::InputDevice::VT_impl::read_error(_self, err);
    }
    static evio::RefCountReleaser data_received(evio::InputDevice* _self, char const* new_data, size_t rlen)
    {
      DoutEntering(dc::notice|flush_cf, "TestSocket::data_received(" << (void*)_self << ", \"" << libcwd::buf2str(new_data, rlen) << "\", " << rlen << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_data_received_count++;
      return evio::InputDevice::VT_impl::data_received(_self, new_data, rlen);
    }
    static void write_to_fd(evio::OutputDevice* _self, int fd)
    {
      DoutEntering(dc::notice|flush_cf, "TestSocket::write_to_fd(" << (void*)_self << ", " << fd << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_write_to_fd_count++;
      OutputDevice::VT_impl::write_to_fd(_self, fd);
    }
    static void write_error(OutputDevice* _self, int err)
    {
      DoutEntering(dc::notice|flush_cf, "TestSocket::write_error(" << _self << ", " << err << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_write_error_count++;
      OutputDevice::VT_impl::write_error(_self, err);
    }

    static constexpr VT_type VT{
      /*TestSocket*/
        /*InputDevice*/
        nullptr,
        read_from_fd,
        read_returned_zero,
        read_error,
        data_received,
        /*OutputDevice*/
        nullptr,
        write_to_fd,
        write_error
    };
  };

  // Make a deep copy of VT_ptr.
  VT_type* clone_VT() override { return VT_ptr.clone(this); }

  utils::VTPtr<TestSocket, evio::InputDevice, evio::OutputDevice> VT_ptr;

 public:
  TestSocket() : m_socket_fd(-1), m_write_to_fd_count(0), m_read_from_fd_count(0), m_read_returned_zero_count(0),
                 m_read_error_count(0), m_data_received_count(0), m_write_error_count(0),
                 VT_ptr(this) { DoutEntering(dc::evio, "TestSocket::TestSocket() [" << this << "]"); }

  ~TestSocket() { DoutEntering(dc::evio, "TestSocket::~TestSocket()"); }

  void open()
  {
    DoutEntering(dc::notice, "TestSocket::open()");
    m_socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    FileDescriptor::init(m_socket_fd);
    evio::SingleThread type;
    start_input_device(type);
    start_output_device(type);
    evio::SocketAddress sa("127.0.0.1:9001");
    int res = connect(m_socket_fd, sa, evio::size_of_addr(sa));
    if (res == -1 && errno != EINPROGRESS)
      THROW_ALERTE("connect([FD], [SA], [SA_SIZE])", AIArgs("[FD]", m_socket_fd)("[SA]", sa)("[SA_SIZE]", evio::size_of_addr(sa)));
  }

  void close()
  {
    DoutEntering(dc::notice, "TestSocket::close()");
    FileDescriptor::close();
  }

  void close_fd()
  {
    ::close(m_socket_fd);
  }

  int write_to_fd_count() const { return m_write_to_fd_count; }
  int read_from_fd_count() const { return m_read_from_fd_count; }
  int read_returned_zero_count() const { return m_read_returned_zero_count; }
  int read_error_count() const { return m_read_error_count; }
  int data_received_count() const { return m_data_received_count; }
  int write_error_count() const { return m_write_error_count; }
};

class TestInputDecoder : public evio::InputDecoder
{
 private:
  size_t m_received;
  bool m_stop_after_one_message;

 public:
  TestInputDecoder() : m_received(0), m_stop_after_one_message(true) { }

  void dont_stop() { m_stop_after_one_message = false; }

 protected:
  evio::RefCountReleaser decode(evio::MsgBlock&& msg, evio::GetThread) override
  {
    // Just print what was received.
    DoutEntering(dc::notice, "TestInputDecoder::decode(\"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
    m_received += msg.get_size();
    // Stop after receiving just one message.
    evio::RefCountReleaser allow_deletion;
    if (m_stop_after_one_message)
      allow_deletion = stop_input_device();
    return allow_deletion;
  }
};

#include "HtmlPipeLineServerFixture.h"

class EventLoopFixture : public HtmlPipeLineServerFixture
{
 private:
  AIThreadPool m_thread_pool;
  evio::EventLoop* m_event_loop;
  AIQueueHandle m_low_priority_handler;

 protected:
  void SetUp()
  {
    DoutEntering(dc::notice, "EventLoopFixture::SetUp()");
    HtmlPipeLineServerFixture::SetUp();
    [[maybe_unused]] AIQueueHandle high_priority_handler = m_thread_pool.new_queue(32);
    [[maybe_unused]] AIQueueHandle medium_priority_handler = m_thread_pool.new_queue(32);
    m_low_priority_handler = m_thread_pool.new_queue(16);
    m_event_loop = new evio::EventLoop(m_low_priority_handler);
  }

  void TearDown()
  {
    DoutEntering(dc::notice, "EventLoopFixture::SetUp()");
    // Fake "leaving the scope" of AIThreadPool and EventLoop...
    // Don't use this yourself.
    // The correct way to do this is:
    //
    // {
    //   AIThreadPool thread_pool;
    //   AIQueueHandle handler = thread_pool.new_queue(32);     // For example.
    //   EventLoop event_loop(handler);
    //
    //   ...your code here...
    //
    //   event_loop.join();
    // }

    m_event_loop->join();
    delete m_event_loop;
    m_low_priority_handler.set_to_undefined();
    {
      // Destruct the thread pool.
      m_thread_pool.change_number_of_threads_to(0);
      AIThreadPool dummy(std::move(m_thread_pool));
    }
    HtmlPipeLineServerFixture::TearDown();
  }
};

class IODeviceFixture : public EventLoopFixture
{
 protected:
  TestInputDecoder decoder;
  evio::OutputStream output;
  boost::intrusive_ptr<TestSocket> io_device;

 protected:
  void SetUp()
  {
    EventLoopFixture::SetUp();
    io_device = evio::create<TestSocket>();
    io_device->input(decoder);
    io_device->output(output);
    CALL(io_device->open());                                            // Because open() calls start_input_device()...
  }

  void TearDown()
  {
    Dout(dc::notice|flush_cf, "Removing last boost::intrusive_ptr for " << io_device.get());
    io_device.reset();
    Dout(dc::notice|flush_cf, "Removed last boost::intrusive_ptr...");
    EventLoopFixture::TearDown();
  }
};

TEST_F(IODeviceFixture, write_to_fd_read_from_fd)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(100));        // ... during this sleep TestSocket::write_to_fd is called once, sees EOF and calls stop_input_device.
  EXPECT_EQ(io_device->write_to_fd_count(), 1);

  output << generate_request(0, 200);                                 // This just writes to the buffer...
  std::this_thread::sleep_for(std::chrono::milliseconds(100));        // ... and does not lead to another call to TestSocket::write_to_fd.
  EXPECT_EQ(io_device->write_to_fd_count(), 1);
  EXPECT_EQ(io_device->read_from_fd_count(), 0);
  EXPECT_EQ(io_device->read_returned_zero_count(), 0);

  output << std::flush;                                               // This writes to the buffer and calls start_input_device(),
  output << request_close(200);                                       // This is just written to the buffer again.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));          // so that TestSocket::write_to_fd is called again to write the buffer to the socket.
  EXPECT_EQ(io_device->write_to_fd_count(), 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(199));        // That message is sent to the http server, which sends 200 ms later a message back that we receive again
  EXPECT_EQ(io_device->read_from_fd_count(), 0);                      // but not yet...
  std::this_thread::sleep_for(std::chrono::milliseconds(2));          // but now we should have...
  EXPECT_EQ(io_device->read_from_fd_count(), 1);                      // causing a call to TestSocket::read_from_fd,
  EXPECT_EQ(io_device->data_received_count(), 1);                     // which called data_received()
  EXPECT_EQ(io_device->read_returned_zero_count(), 0);                // and not read_returned_zero().
}

TEST_F(IODeviceFixture, read_returned_zero)
{
  std::this_thread::sleep_for(std::chrono::milliseconds(100));        // ... during this sleep TestSocket::write_to_fd is called once, sees EOF and calls stop_input_device.
  EXPECT_EQ(io_device->write_to_fd_count(), 1);

  output << request_close(200);                                       // This is just written to the buffer again.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));        // ... and does not lead to another call to TestSocket::write_to_fd.
  EXPECT_EQ(io_device->write_to_fd_count(), 1);
  EXPECT_EQ(io_device->read_from_fd_count(), 0);
  EXPECT_EQ(io_device->read_returned_zero_count(), 0);

  output << std::flush;                                               // This writes to the buffer and calls start_input_device(),
  std::this_thread::sleep_for(std::chrono::milliseconds(1));          // so that TestSocket::write_to_fd is called again to write the buffer to the socket.
  EXPECT_EQ(io_device->write_to_fd_count(), 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(199));        // That message is sent to the http server, which closes the socket 200 ms later...
  EXPECT_EQ(io_device->read_from_fd_count(), 0);                      // but not yet...
  std::this_thread::sleep_for(std::chrono::milliseconds(2));          // but now we should have...
  EXPECT_EQ(io_device->read_from_fd_count(), 1);                      // causing a call to TestSocket::read_from_fd,
  EXPECT_EQ(io_device->read_returned_zero_count(), 1);                // which called read_returned_zero()
  EXPECT_EQ(io_device->data_received_count(), 0);                     // and not data_received().
}

TEST_F(IODeviceFixture, request_with_close)
{
  decoder.dont_stop();                                                // Don't close the fd after reading one message.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  output << generate_request_with_close(1, 10) << std::flush;         // Server will wait 10 ms then write one message and immediately close the fd.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(io_device->write_to_fd_count(), 2);                       // Same as above tests.
  EXPECT_EQ(io_device->read_from_fd_count(), 2);                      // One for the message and one for the close.
  EXPECT_EQ(io_device->data_received_count(), 1);                     // One for the message.
  EXPECT_EQ(io_device->read_returned_zero_count(), 1);                // One for the close.
}

TEST_F(IODeviceFixture, read_error_write_error)
{
  decoder.dont_stop();                                                // Don't close the fd after reading one message.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  output << generate_request_with_close(1, 10) << std::flush;         // Server will wait 10 ms then write one message and immediately close the fd.
  io_device->close_fd();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(io_device->write_to_fd_count(), 2);                       // Same as above tests.
  EXPECT_EQ(io_device->read_from_fd_count(), 1);                      // One for the close.
  EXPECT_EQ(io_device->data_received_count(), 0);                     // No message received.
  EXPECT_EQ(io_device->read_returned_zero_count(), 0);                // No remote close.
  EXPECT_EQ(io_device->read_error_count(), 1);                        // We were woken up, but trying to read caused a EBADF error.
  EXPECT_EQ(io_device->write_error_count(), 1);                       // The attempt to write the message failed with a EBADF error.
}