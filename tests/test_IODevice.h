#include "evio/InputDevice.h"
#include "evio/SocketAddress.h"
#include "evio/inet_support.h"
#include "evio/EventLoop.h"
#ifdef CWDEBUG
#include "libcwd/buf2str.h"
#endif
#include "NoEpollDevices.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
// timestamp
#include <sys/time.h>
#include <ctime>
#include <cstdio>

#ifndef DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
#define DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS 0
#endif

using evio::RefCountReleaser;

#include "MyDummyDecoder.h"

std::string timestamp()
{
  struct timeval tv;
  time_t nowtime;
  struct tm *nowtm;
  char tmbuf[64], buf[80];

  gettimeofday(&tv, NULL);
  nowtime = tv.tv_sec;
  nowtm = localtime(&nowtime);
  strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", nowtm);
  snprintf(buf, sizeof(buf), "%s.%06lu: ", tmbuf, tv.tv_usec);
  return buf;
}

// Work around the fact that InputDevice and OutputDevice have a protected constructor and protected methods.

struct InputDevice : public NoEpollInputDevice
{
  using VT_type = evio::InputDevice::VT_type;
  #define VT_InputDevice VT_NoEpollInputDevice

  struct VT_impl : evio::InputDevice::VT_impl
  {
    static void read_from_fd(int& UNUSED_ARG(allow_deletion_count), evio::InputDevice* UNUSED_ARG(self), int UNUSED_ARG(fd)) { } // override

    // Virtual table of ListenSocketDevice.
    static constexpr VT_type VT VT_InputDevice;
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<InputDevice, evio::InputDevice> VT_ptr;

  InputDevice() : VT_ptr(this) { }

  MyDummyDecoder m_decoder;

  void start_input_device()
  {
    evio::InputDevice::start_input_device();
  }

  void stop_input_device()
  {
    evio::InputDevice::stop_input_device();
  }

  RefCountReleaser remove_input_device()
  {
    int allow_deletion_count = 0;
    evio::InputDevice::remove_input_device(allow_deletion_count, state_t::wat(m_state));
    return {this, allow_deletion_count};
  }

  void disable_input_device()
  {
    evio::InputDevice::disable_input_device();
  }

  void enable_input_device()
  {
    evio::InputDevice::enable_input_device();
  }

  void init(int fd)
  {
    evio::InputDevice::init(fd);
    set_sink(m_decoder);
#if 0
    // Set this to regular_file in order to avoid epoll being called (which isn't initialized).
    state_t::wat state_w(m_state);
    state_w->m_flags.set_regular_file();
#endif
  }

#ifdef CWDEBUG
  int get_fd() const
  {
    return evio::InputDevice::get_fd();
  }

  evio::FileDescriptorFlags const get_flags()
  {
    return evio::FileDescriptor::get_flags();
  }
#endif
};

struct OutputDevice : public NoEpollOutputDevice
{
  using VT_type = evio::OutputDevice::VT_type;
  #define VT_OutputDevice VT_NoEpollOutputDevice

  struct VT_impl : public evio::OutputDevice::VT_impl
  {
    static void write_to_fd(int& UNUSED_ARG(allow_deletion_count), evio::OutputDevice* UNUSED_ARG(self), int UNUSED_ARG(fd)) { } // override

    static constexpr VT_type VT VT_OutputDevice;
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<OutputDevice, evio::OutputDevice> VT_ptr;

  OutputDevice() : VT_ptr(this) { }

  evio::OutputStream m_output;

  void start_output_device()
  {
    evio::OutputDevice::start_output_device();
  }

  RefCountReleaser stop_output_device()
  {
    int allow_deletion_count = 0;
    evio::OutputDevice::stop_output_device(allow_deletion_count);
    return {this, allow_deletion_count};
  }

  RefCountReleaser remove_output_device()
  {
    int allow_deletion_count = 0;
    evio::OutputDevice::remove_output_device(allow_deletion_count);
    return {this, allow_deletion_count};
  }

  RefCountReleaser flush_output_device()
  {
    return evio::OutputDevice::flush_output_device();
  }

  void disable_output_device()
  {
    evio::OutputDevice::disable_output_device();
  }

  void enable_output_device()
  {
    evio::OutputDevice::enable_output_device();
  }

  void init(int fd)
  {
    evio::OutputDevice::init(fd);
    set_source(m_output);
  }

#ifdef CWDEBUG
  int get_fd() const
  {
    return evio::OutputDevice::get_fd();
  }

  evio::FileDescriptorFlags const get_flags()
  {
    return evio::FileDescriptor::get_flags();
  }
#endif
};

// Test Fixture

#include "EventLoopFixture.h"

class TestIODeviceNoInit : public EventLoopFixture<testing::Test>
{
 protected:
  int pipefd[2];
  boost::intrusive_ptr<InputDevice> m_input_device;
  boost::intrusive_ptr<OutputDevice> m_output_device;

 protected:
  void SetUp() override
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v TestIODeviceNoInit::SetUp()");
    debug::Mark setup;
#endif
    EventLoopFixture<testing::Test>::SetUp();
    int res = pipe2(pipefd, O_DIRECT | O_CLOEXEC);
    if (res == -1)
      perror("pipe2");
    ASSERT(res == 0);
    m_input_device = evio::create<InputDevice>();
    m_output_device = evio::create<OutputDevice>();
  }

  void TearDown() override
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v TestIODeviceNoInit::TearDown()");
    debug::Mark setup;
#endif
    // Delete the device objects.
    m_input_device.reset();
    m_output_device.reset();
    // Clean up the pipe.
    if (evio::is_valid(pipefd[0]))
      close(pipefd[0]);
    if (evio::is_valid(pipefd[1]))
      close(pipefd[1]);
    EventLoopFixture<testing::Test>::TearDown();
  }
};

class TestIODevice : public TestIODeviceNoInit
{
 protected:
  void SetUp() override
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v TestIODevice::SetUp()");
    debug::Mark setup;
#endif
    TestIODeviceNoInit::SetUp();
    m_input_device->init(pipefd[0]);
    m_output_device->init(pipefd[1]);
  }

  void TearDown() override
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v TestIODevice::TearDown()");
    debug::Mark setup;
#endif
    if (m_input_device->is_active(evio::SingleThread()).is_momentary_true())
    {
      RefCountReleaser rcr = m_input_device->close_input_device();
      // is_momentary_true() here really means is_true() -- because we don't have an EventLoopThread running during this test, nor any other threads.
      // Therefore, this should always be true.
      EXPECT_TRUE(rcr); // To cancel inhibit_deletion of being added.
    }
    if (m_output_device->is_active(evio::SingleThread()).is_momentary_true())
    {
      RefCountReleaser rcr = m_output_device->close_output_device();
      // See comment above.
      EXPECT_TRUE(rcr); // To cancel inhibit_deletion of being added.
    }
    // Perform a garbage_collection().
    evio::EventLoopThread::instance().wake_up();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // Make sure these are really the last references.
    EXPECT_TRUE(m_input_device->unique().is_true());
    EXPECT_TRUE(m_output_device->unique().is_true());
    m_input_device.reset();             // Release last reference to device; this should close it.
    m_output_device.reset();            // Release last reference to device; this should close it.
    TestIODeviceNoInit::TearDown();
  }
};

#ifdef CWDEBUG

TEST_F(TestIODeviceNoInit, SetUpAndTearDown)
{
  EXPECT_EQ(m_input_device->get_fd(), -1);
  EXPECT_EQ(m_output_device->get_fd(), -1);
}

TEST_F(TestIODevice, SetUpAndTearDown)
{
  EXPECT_EQ(m_input_device->get_fd(), pipefd[0]);
  EXPECT_EQ(m_output_device->get_fd(), pipefd[1]);
}

#endif // CWDEBUG

TEST_F(TestIODevice, StartStop)
{
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
  //   The PutThread      .--------.     The EventLoopThread
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
  // it is said that the underlaying FileDescriptor object is made 'active'.
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
  // The important part of the above is where a transition is exclusive to either
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

  m_input_device->start_input_device();
  m_output_device->start_output_device();

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

  // Stopping the devices didn't remove them from the epoll interest list,
  // and therefore the inhibit_deletion is still in effect.
  EXPECT_TRUE(m_input_device->unique().is_momentary_false());
  EXPECT_TRUE(m_output_device->unique().is_momentary_false());

  m_input_device->remove_input_device();
  m_output_device->remove_output_device();

  // Also wait for all events that were queued in the thread pool,
  // which also caused an inhibit_deletion().
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Now that the devices are removed, the inhibit was removed. remove
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());
}

TEST_F(TestIODevice, DisableStartEnableStartStop)
{
  // If a device is disabled before starting...
  m_input_device->disable_input_device();
  m_output_device->disable_output_device();
  // Then the devices are not kept alive.
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());

  // Starting while being disabled prints a WARNING (you shouldn't do that).
  m_input_device->start_input_device();
  m_output_device->start_output_device();
  // And they weren't started.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_false());
  // Nor was inhibit_deletion() called therefore.
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());

  // Enabling the devices again...
  m_input_device->enable_input_device();
  m_output_device->enable_output_device();
  // DOES start them however.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_transitory_true());
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_transitory_true());
  // Which also inhibits deletion.
  EXPECT_TRUE(m_input_device->unique().is_momentary_false());
  EXPECT_TRUE(m_output_device->unique().is_momentary_false());

  // Starting devices that are already started does nothing.
  m_input_device->start_input_device();
  m_output_device->start_output_device();

  // Stopping them...
  m_input_device->stop_input_device();
  m_output_device->stop_output_device();
  // Makes them inactive again.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_false());
  // And the inhibit_deletion was not cancelled yet.
  EXPECT_TRUE(m_input_device->unique().is_momentary_false());
  EXPECT_TRUE(m_output_device->unique().is_momentary_false());

  // Removing them...
  m_input_device->remove_input_device();
  m_output_device->remove_output_device();

  // Also wait for the read_event() that was queued in the thread pool,
  // which also caused an inhibit_deletion().
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // And the inhibit_deletion was cancelled.
  EXPECT_TRUE(m_input_device->unique().is_true());
  EXPECT_TRUE(m_output_device->unique().is_true());
}

TEST_F(TestIODevice, StartDisableEnableClose)
{
  // Disabling devices that are already started...
  m_input_device->start_input_device();
  m_output_device->start_output_device();
  m_input_device->disable_input_device();
  m_output_device->disable_output_device();
  // Should still keep them alive.
  EXPECT_TRUE(m_input_device->unique().is_momentary_false());
  EXPECT_TRUE(m_output_device->unique().is_momentary_false());
  // Despite that they aren't active.
  EXPECT_TRUE(m_input_device->is_active(evio::SingleThread()).is_false());
  EXPECT_TRUE(m_output_device->is_active(evio::SingleThread()).is_false());

  m_input_device->enable_input_device();
  m_output_device->enable_output_device();
  m_input_device->close_input_device();
  m_output_device->close_output_device();
}

TEST_F(TestIODevice, StartDisableClose)
{
  m_input_device->start_input_device();
  m_output_device->start_output_device();
  m_input_device->disable_input_device();
  m_output_device->disable_output_device();
  m_input_device->close_input_device();
  m_output_device->close_output_device();
}

TEST_F(TestIODevice, StartDisableEnableStop)
{
  m_input_device->start_input_device();
  m_output_device->start_output_device();
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
  int m_real_fd;
  std::function<void()> m_write_error_detected;
  std::atomic_int m_write_to_fd_count;
  std::atomic_int m_read_from_fd_count;
  std::atomic_int m_hup_count;
  std::atomic_int m_read_returned_zero_count;
  std::atomic_int m_read_error_count;
  std::atomic_int m_data_received_count;
  std::atomic_int m_write_error_count;

 public:
  struct VT_type : evio::InputDevice::VT_type, evio::OutputDevice::VT_type
  {
    #define VT_TestSocket { VT_evio_InputDevice, VT_evio_OutputDevice }
  };

  struct VT_impl : evio::InputDevice::VT_impl, evio::OutputDevice::VT_impl
  {
    static void read_from_fd(int& allow_deletion_count, evio::InputDevice* _self, int fd)
    {
      DoutEntering(dc::notice|flush_cf, timestamp() << "TestSocket::read_from_fd({" << allow_deletion_count << "}, " << (void*)_self << ", " << fd << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_read_from_fd_count++;
      evio::InputDevice::VT_impl::read_from_fd(allow_deletion_count, _self, fd);
    }
    static void hup(int& CWDEBUG_ONLY(allow_deletion_count), InputDevice* _self, int CWDEBUG_ONLY(fd))
    {
      DoutEntering(dc::notice|flush_cf, timestamp() << "TestSocket::hup({" << allow_deletion_count << "}, " << (void*)_self << ", " << fd << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_hup_count++;
      // Allow the writing thread to get its write error before the HUP closes the fd.
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    static void err(int& CWDEBUG_ONLY(allow_deletion_count), InputDevice* _self, int CWDEBUG_ONLY(fd))
    {
      DoutEntering(dc::notice|flush_cf, timestamp() << "TestSocket::err({" << allow_deletion_count << "}, " << (void*)_self << ", " << fd << ")");
      // Suppress error event.
    }
    static void read_returned_zero(int& allow_deletion_count, evio::InputDevice* _self)
    {
      DoutEntering(dc::notice|flush_cf, timestamp() << "TestSocket::read_returned_zero({" << allow_deletion_count << "}, " << (void*)_self << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_read_returned_zero_count++;
      evio::InputDevice::VT_impl::read_returned_zero(allow_deletion_count, _self);
    }
    static void read_error(int& allow_deletion_count, evio::InputDevice* _self, int err)
    {
      DoutEntering(dc::notice|flush_cf, timestamp() << "TestSocket::read_error({" << allow_deletion_count << "}, " << (void*)_self << ", " << err << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_read_error_count++;
      self->unscrew_fd();
      evio::InputDevice::VT_impl::read_error(allow_deletion_count, _self, err);
    }
    static void data_received(int& allow_deletion_count, evio::InputDevice* _self, char const* new_data, size_t rlen)
    {
      DoutEntering(dc::notice|flush_cf, timestamp() << "TestSocket::data_received({" << allow_deletion_count << "}, " << (void*)_self << ", \"" <<
          libcwd::buf2str(new_data, rlen) << "\", " << rlen << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_data_received_count++;
      evio::InputDevice::VT_impl::data_received(allow_deletion_count, _self, new_data, rlen);
    }
    static void write_to_fd(int& allow_deletion_count, evio::OutputDevice* _self, int fd)
    {
      DoutEntering(dc::notice|flush_cf, timestamp() << "TestSocket::write_to_fd({" << allow_deletion_count << "}, " << (void*)_self << ", " << fd << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_write_to_fd_count++;
      OutputDevice::VT_impl::write_to_fd(allow_deletion_count, _self, fd);
    }
    static void write_error(int& allow_deletion_count, OutputDevice* _self, int err)
    {
      DoutEntering(dc::notice|flush_cf, timestamp() << "TestSocket::write_error({" << allow_deletion_count << "}, " << _self << ", " << err << ")");
      TestSocket* self = static_cast<TestSocket*>(_self);
      self->m_write_error_count++;
      self->m_write_error_detected();
      OutputDevice::VT_impl::write_error(allow_deletion_count, _self, err);
    }

    static constexpr VT_type VT VT_TestSocket;
  };

  // These two lines must be in THIS order (clone_VT first)!
  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<TestSocket, evio::InputDevice, evio::OutputDevice> VT_ptr;

 public:
  TestSocket() : m_socket_fd(-1), m_write_to_fd_count(0), m_read_from_fd_count(0), m_hup_count(0), m_read_returned_zero_count(0),
                 m_read_error_count(0), m_data_received_count(0), m_write_error_count(0),
                 VT_ptr(this) { DoutEntering(dc::evio, "TestSocket::TestSocket() [" << this << "]"); }

  ~TestSocket()
  {
    DoutEntering(dc::evio, "TestSocket::~TestSocket()");
    if (m_socket_fd != -1)
    {
      Dout(dc::system|continued_cf, "::close(" << m_socket_fd << ") = ");
      CWDEBUG_ONLY(int res =) ::close(m_socket_fd);
      Dout(dc::finish|cond_error_cf(res == -1), res);
    }
  }

  void init()
  {
    DoutEntering(dc::notice, "TestSocket::init()");
    m_socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    FileDescriptor::init(m_socket_fd);
  }

  void listen()
  {
    DoutEntering(dc::notice, "TestSocket::listen()");
    init();
    ::listen(m_socket_fd, 4);
    start_input_device();
  }

  void connect()
  {
    DoutEntering(dc::notice, "TestSocket::connect()");
    init();
    evio::SocketAddress sa("127.0.0.1:9001");
    int res = ::connect(m_socket_fd, sa, evio::size_of_addr(sa));
    if (res == -1 && errno != EINPROGRESS)
      THROW_ALERTE("connect([FD], [SA], [SA_SIZE])", AIArgs("[FD]", m_socket_fd)("[SA]", sa)("[SA_SIZE]", evio::size_of_addr(sa)));
    start_input_device();
    start_output_device();
  }

  void close()
  {
    DoutEntering(dc::notice, "TestSocket::close()");
    FileDescriptor::close();
  }

  void screw_fd()
  {
    m_real_fd = m_fd;
    m_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    ::close(m_fd);
    Dout(dc::notice, "screw_fd(): overwrote m_fd with bad fd " << m_fd);
  }

  void unscrew_fd()
  {
    m_fd = m_real_fd;
    Dout(dc::notice, "unscrew_fd(): restored m_fd with fd " << m_fd);
  }

  void set_write_error_callback(std::function<void()> cb)
  {
    m_write_error_detected = cb;
  }

  int write_to_fd_count() const { return m_write_to_fd_count; }
  int read_from_fd_count() const { return m_read_from_fd_count; }
  int hup_count() const { return m_hup_count; }
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
  void decode(int& allow_deletion_count, evio::MsgBlock&& msg) override
  {
    // Just print what was received.
    DoutEntering(dc::notice, timestamp() << "TestInputDecoder::decode({" << allow_deletion_count << "}, \"" << buf2str(msg.get_start(), msg.get_size()) << "\") [" << this << ']');
    m_received += msg.get_size();
    // Stop after receiving just one message.
    if (m_stop_after_one_message)
      close_input_device(allow_deletion_count);
  }
};

#include "HtmlPipeLineServerFixture.h"

class IODeviceFixture : public EventLoopFixture<HtmlPipeLineServerFixture>
{
 protected:
  TestInputDecoder decoder;
  evio::OutputStream output;
  boost::intrusive_ptr<TestSocket> io_device;
  boost::intrusive_ptr<TestSocket> keep_alive_device;

 protected:
  void SetUp()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v IODeviceFixture::SetUp()");
    debug::Mark setup;
#endif
    EventLoopFixture<HtmlPipeLineServerFixture>::SetUp();
    io_device = evio::create<TestSocket>();
    Dout(dc::notice, "io_device = " << io_device.get());
    io_device->set_sink(decoder);
    io_device->set_source(output);
    CALL(io_device->connect());                                       // Because connect() calls start_input_device()...
  }

  void keep_alive()
  {
    Dout(dc::notice, "IODeviceFixture::keep_alive()");
    keep_alive_device = evio::create<TestSocket>();
    keep_alive_device->set_sink(decoder);
    CALL(keep_alive_device->listen());
  }

  void TearDown()
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v IODeviceFixture::TearDown()");
    debug::Mark setup;
#endif
    Dout(dc::notice|flush_cf, "Removing last boost::intrusive_ptr for " << io_device.get());
    io_device.reset();
    Dout(dc::notice|flush_cf, "Removed last boost::intrusive_ptr...");
    EventLoopFixture<HtmlPipeLineServerFixture>::TearDown();
  }
};

TEST_F(IODeviceFixture, write_to_fd_read_from_fd)
{
  decoder.dont_stop();                                                // Don't close the fd after reading one message.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));        // ... during this sleep TestSocket::write_to_fd is called once, sees EOF and calls stop_input_device.
  EXPECT_EQ(io_device->write_to_fd_count(), 1);

  output << generate_request(0, 200);                                 // This just writes to the buffer...
  std::this_thread::sleep_for(std::chrono::milliseconds(100));        // ... and does not lead to another call to TestSocket::write_to_fd.
  EXPECT_EQ(io_device->write_to_fd_count(), 1);
  EXPECT_EQ(io_device->read_from_fd_count(), 0);
  EXPECT_EQ(io_device->read_returned_zero_count(), 0);

  output << std::flush;                                               // This writes to the buffer and calls start_input_device()...
  output << request_close(200);                                       // (this is just written to the buffer again)
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  EXPECT_EQ(io_device->write_to_fd_count(), 2);                       // ...so that TestSocket::write_to_fd is called again to write the buffer to the socket.
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  std::this_thread::sleep_for(std::chrono::milliseconds(199));        // That message is sent to the http server, which sends 200 ms later a message back that we receive again
  EXPECT_EQ(io_device->read_from_fd_count(), 0);                      // but not yet...
  Dout(dc::notice, timestamp() << "Start 2 ms sleep.");
  std::this_thread::sleep_for(std::chrono::milliseconds(2));          // but now we should have...
  // Wait till all threads are finished.
  size_t wt =100;
  while (io_device->is_busy())
    std::this_thread::sleep_for(std::chrono::microseconds(wt *= 2));
  // This is >= 1, because sometimes read() returns EAGAIN - causing a second call to read_from_fd.
  Dout(dc::notice, "Running final checks...");
  EXPECT_GE(io_device->read_from_fd_count(), 1);                      // causing a call to TestSocket::read_from_fd,
  EXPECT_EQ(io_device->data_received_count(), 1);                     // which called data_received().
  EXPECT_EQ(io_device->read_returned_zero_count(), 1);                // read_from_fd continues reading till we reached EOF (or EGAIN).
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
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  std::this_thread::sleep_for(std::chrono::milliseconds(1));          // so that TestSocket::write_to_fd is called again to write the buffer to the socket.
  EXPECT_EQ(io_device->write_to_fd_count(), 2);
  std::this_thread::sleep_for(std::chrono::milliseconds(199));        // That message is sent to the http server, which closes the socket 200 ms later...
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  // Very sometimes this is 1 because the test application stalled for 1 millisecond somewhere.
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
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(io_device->write_to_fd_count(), 2);                       // Same as above tests.
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  // A value of 2 instead of 1 might happen sometimes.
  EXPECT_GE(io_device->read_from_fd_count(), 1);                      // One for the message, and we now continue to read till EAGAIN - so also get the EOF (this is racy though).
  EXPECT_EQ(io_device->data_received_count(), 1);                     // One for the message.
  EXPECT_EQ(io_device->read_returned_zero_count(), 1);                // One for the close.
}

TEST_F(IODeviceFixture, read_error)
{
  decoder.dont_stop();                                                // Don't close the fd after reading one message.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  output << generate_request_with_close(1, 10) << std::flush;         // Server will wait 10 ms then write one message and immediately close the fd.
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  std::this_thread::sleep_for(std::chrono::milliseconds(1));          // Give the library the time to write this message to the server.
  io_device->screw_fd();                                              // Screw our fd.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  EXPECT_EQ(io_device->write_to_fd_count(), 2);                       // Same as above tests.
  EXPECT_EQ(io_device->read_from_fd_count(), 1);                      // We were woken up, but trying to read caused a EBADF error.
  EXPECT_EQ(io_device->data_received_count(), 0);                     // No message received.
  EXPECT_EQ(io_device->read_returned_zero_count(), 0);                // No remote close because we get a read error instead of reading EOF.
  EXPECT_EQ(io_device->read_error_count(), 1);                        // We were woken up, but trying to read caused a EBADF error.
  EXPECT_EQ(io_device->write_error_count(), 0);
}

TEST_F(IODeviceFixture, write_error)
{
  keep_alive();
  io_device->close_input_device();
  io_device->set_write_error_callback([&](){ keep_alive_device->close(); });
  decoder.dont_stop();                                                // Don't close the fd after reading one message.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  output << generate_request_with_close(1, 1) << std::flush;          // Server will wait 1 ms then write one message and immediately close the fd.
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  output << std::string(100000, 'x') << std::flush;
#if DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS
  std::this_thread::sleep_for(std::chrono::microseconds(DEBUG_EPOLL_PWAIT_DELAY_MICROSECONDS));
#endif
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_GE(io_device->write_to_fd_count(), 2);                       // One EOF at the start and then at least one for the burst until that runs into a write error.
  EXPECT_EQ(io_device->write_error_count(), 1);
  EXPECT_EQ(io_device->hup_count(), 1);                               // Probably called in parallel (while we were still writing to the socket).
}

#ifdef CWDEBUG
void test_is_Closed(boost::intrusive_ptr<InputDevice> input_device)
{
  EXPECT_FALSE(input_device->get_flags().is_r_open());
  EXPECT_FALSE(input_device->get_flags().is_r_added());
  EXPECT_FALSE(input_device->get_flags().is_active_input_device());
  EXPECT_TRUE(input_device->get_flags().is_dead());
}

void test_is_Closed(boost::intrusive_ptr<OutputDevice> output_device)
{
  EXPECT_FALSE(output_device->get_flags().is_w_open());
  EXPECT_FALSE(output_device->get_flags().is_w_added());
  EXPECT_FALSE(output_device->get_flags().is_active_output_device());
  EXPECT_FALSE(output_device->get_flags().is_w_flushing());
  EXPECT_TRUE(output_device->get_flags().is_dead());
}

void test_is_Removed(boost::intrusive_ptr<InputDevice> input_device)
{
  EXPECT_TRUE(input_device->get_flags().is_r_open());
  EXPECT_FALSE(input_device->get_flags().is_r_added());
  EXPECT_FALSE(input_device->get_flags().is_active_input_device());
}

void test_is_Removed(boost::intrusive_ptr<OutputDevice> output_device)
{
  EXPECT_TRUE(output_device->get_flags().is_w_open());
  EXPECT_FALSE(output_device->get_flags().is_w_added());
  EXPECT_FALSE(output_device->get_flags().is_active_output_device());
  EXPECT_FALSE(output_device->get_flags().is_w_flushing());
}

void test_is_Added(boost::intrusive_ptr<InputDevice> input_device)
{
  EXPECT_TRUE(input_device->get_flags().is_r_open());
  EXPECT_TRUE(input_device->get_flags().is_r_added());
  EXPECT_FALSE(input_device->get_flags().is_active_input_device());
}

void test_is_Added(boost::intrusive_ptr<OutputDevice> output_device)
{
  EXPECT_TRUE(output_device->get_flags().is_w_open());
  EXPECT_TRUE(output_device->get_flags().is_w_added());
  EXPECT_FALSE(output_device->get_flags().is_active_output_device());
  EXPECT_FALSE(output_device->get_flags().is_w_flushing());
}

void test_is_Active(boost::intrusive_ptr<InputDevice> input_device)
{
  EXPECT_TRUE(input_device->get_flags().is_r_open());
  EXPECT_TRUE(input_device->get_flags().is_r_added());
  EXPECT_TRUE(input_device->get_flags().is_active_input_device());
}

void test_is_Active(boost::intrusive_ptr<OutputDevice> output_device)
{
  EXPECT_TRUE(output_device->get_flags().is_w_open());
  EXPECT_TRUE(output_device->get_flags().is_w_added());
  EXPECT_TRUE(output_device->get_flags().is_active_output_device());
  EXPECT_FALSE(output_device->get_flags().is_w_flushing());
}

void test_is_Flushing(boost::intrusive_ptr<OutputDevice> output_device)
{
  EXPECT_TRUE(output_device->get_flags().is_w_open());
  EXPECT_TRUE(output_device->get_flags().is_w_added());
  EXPECT_TRUE(output_device->get_flags().is_active_output_device());
  EXPECT_TRUE(output_device->get_flags().is_w_flushing());
}

TEST_F(TestIODevice, DeviceStatesRemovedClose)
{
  // Test that input and output device are in state 'Removed'.
  CALL(test_is_Removed(m_input_device));
  CALL(test_is_Removed(m_output_device));

  m_input_device->stop_input_device();
  EXPECT_FALSE(m_output_device->stop_output_device());
  CALL(test_is_Removed(m_input_device));
  CALL(test_is_Removed(m_output_device));

  EXPECT_FALSE(m_input_device->close_input_device());
  EXPECT_FALSE(m_output_device->close_output_device());
  CALL(test_is_Closed(m_input_device));
  CALL(test_is_Closed(m_output_device));
}

TEST_F(TestIODevice, DeviceStatesAddedClose)
{
  m_input_device->start_input_device();
  m_output_device->start_output_device();
  CALL(test_is_Active(m_input_device));
  CALL(test_is_Active(m_output_device));

  m_input_device->start_input_device();
  m_output_device->start_output_device();
  CALL(test_is_Active(m_input_device));
  CALL(test_is_Active(m_output_device));

  m_input_device->stop_input_device();
  EXPECT_FALSE(m_output_device->stop_output_device());
  CALL(test_is_Added(m_input_device));
  CALL(test_is_Added(m_output_device));

  m_input_device->start_input_device();
  m_output_device->start_output_device();
  CALL(test_is_Active(m_input_device));
  CALL(test_is_Active(m_output_device));

  m_input_device->stop_input_device();
  EXPECT_FALSE(m_output_device->stop_output_device());
  CALL(test_is_Added(m_input_device));
  CALL(test_is_Added(m_output_device));

  RefCountReleaser input_releaser = m_input_device->close_input_device();
  RefCountReleaser output_releaser = m_output_device->close_output_device();
  CALL(test_is_Closed(m_input_device));
  CALL(test_is_Closed(m_output_device));
  EXPECT_TRUE(input_releaser);
  EXPECT_TRUE(output_releaser);
}

TEST_F(TestIODevice, DeviceStatesActiveClose)
{
  m_input_device->start_input_device();
  m_output_device->start_output_device();

  RefCountReleaser input_releaser = m_input_device->close_input_device();
  RefCountReleaser output_releaser = m_output_device->close_output_device();
  CALL(test_is_Closed(m_input_device));
  CALL(test_is_Closed(m_output_device));
  EXPECT_TRUE(input_releaser);
  EXPECT_TRUE(output_releaser);
}

TEST_F(TestIODevice, DeviceStatesFlushingClose)
{
  m_output_device->start_output_device();
  m_output_device->flush_output_device();
  CALL(test_is_Flushing(m_output_device));
  EXPECT_FALSE(m_output_device->flush_output_device());
  CALL(test_is_Flushing(m_output_device));
  m_output_device->start_output_device();
  CALL(test_is_Flushing(m_output_device));
  RefCountReleaser output_releaser = m_output_device->close_output_device();
  CALL(test_is_Closed(m_output_device));
  EXPECT_TRUE(output_releaser);
}

TEST_F(TestIODevice, DeviceStatesFlushingStop)
{
  m_output_device->start_output_device();
  RefCountReleaser output_releaser1 = m_output_device->flush_output_device();
  CALL(test_is_Flushing(m_output_device));
  RefCountReleaser output_releaser2 = m_output_device->stop_output_device();
  CALL(test_is_Closed(m_output_device));
  EXPECT_TRUE(output_releaser2);
}

TEST_F(TestIODevice, DeviceStatesRemovedFlush)
{
  RefCountReleaser output_releaser = m_output_device->flush_output_device();
  CALL(test_is_Closed(m_output_device));
  EXPECT_FALSE(output_releaser);
}

TEST_F(TestIODevice, DeviceStatesAddedFlush)
{
  m_output_device->start_output_device();
  m_output_device->stop_output_device();
  CALL(test_is_Added(m_output_device));
  RefCountReleaser output_releaser = m_output_device->flush_output_device();
  CALL(test_is_Closed(m_output_device));
  EXPECT_TRUE(output_releaser);
}

#endif // CWDEBUG
