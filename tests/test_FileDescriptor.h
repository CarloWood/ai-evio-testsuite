#include "evio/InputDevice.h"
#include "evio/EventLoopThread.h"
#include <cassert>
#include <sys/socket.h>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

using evio::RefCountReleaser;

// FileDescriptor *only* has a default constructor, but it is protected,
// so create this class in order to be able to instantiate it.
class FileDescriptor : public virtual evio::FileDescriptor
{
 private:
  static int s_instance_count;
  int m_fd;

 protected:
  bool m_closed_called;

  void set_dont_close()
  {
    state_t::wat(m_state)->m_flags.set_dont_close();
  }

 public:
  FileDescriptor() : m_fd(-1), m_closed_called(false) { ++s_instance_count; }
  ~FileDescriptor() { --s_instance_count; }

  void tell_testsuite_fd(int fd)
  {
    m_fd = fd;
  }

  void test_fd_is_minus_one() const
  {
    ASSERT(m_fd == -1);
#ifdef CWDEBUG
    EXPECT_EQ(get_fd(), m_fd);
#endif
  }

  void test_fd_is_set() const
  {
    ASSERT(m_fd != -1);
#ifdef CWDEBUG
    EXPECT_EQ(get_fd(), m_fd);
#endif
  }

  void test_close_input_device_does_nothing()
  {
    m_closed_called = false;
    RefCountReleaser rcr = close_input_device();
    EXPECT_FALSE(m_closed_called);      // closed() was not called.
    EXPECT_FALSE(rcr);  // This means that going out of scope won't do anything.
  }

  void test_close_output_device_does_nothing()
  {
    m_closed_called = false;
    RefCountReleaser rcr = close_output_device();
    EXPECT_FALSE(m_closed_called);      // closed() was not called.
    EXPECT_FALSE(rcr);  // This means that going out of scope won't do anything.
  }

  void test_close_does_nothing()
  {
    m_closed_called = false;
    RefCountReleaser rcr = close();
    EXPECT_FALSE(m_closed_called);      // closed() was not called.
    EXPECT_FALSE(rcr);  // This means that going out of scope won't do anything.
  }

  static void reset_instance_count()
  {
    s_instance_count = 0;
  }

  static void test_all_instances_were_destructed()
  {
    evio::EventLoopThread::instance().garbage_collection();
    EXPECT_EQ(s_instance_count, 0);
  }

 protected:
  // Event: the filedescriptor(s) of this device were just closed (close_fds() was called).
  // If INTERNAL_FDS_DONT_CLOSE is set then the fd(s) weren't really closed, but this method is still called.
  // When we get here the object is also marked as FDS_DEAD.
  void closed(int& UNUSED_ARG(allow_deletion_count)) override
  {
    EXPECT_FALSE(m_closed_called);
    m_closed_called = true;
    state_t::wat state_w(m_state);
    EXPECT_TRUE(state_w->m_flags.is_dead());
    bool dont_close = state_w->m_flags.dont_close();
    EXPECT_TRUE(evio::is_valid(m_fd) == dont_close);
  }

 public:
  evio::FileDescriptorFlags const get_flags() { return evio::FileDescriptor::get_flags(); }
};

//static
int FileDescriptor::s_instance_count;

#include "MyDummyDecoder.h"
#include "NoEpollDevices.h"

class TestInputDevice : public NoEpollInputDevice, public FileDescriptor
{
 private:
  MyDummyDecoder m_decoder;
  int m_pipefd[2];

 public:
  TestInputDevice()
  {
    set_sink(m_decoder);
    // Get some valid fd.
    if (pipe2(m_pipefd, O_CLOEXEC) == -1)
      DoutFatal(dc::core|error_cf, "pipe2(" << m_pipefd << ", O_CLOEXEC) = -1");
  }
  ~TestInputDevice()
  {
    Dout(dc::system|continued_cf, "close(" << m_pipefd[1] << ") = ");
    CWDEBUG_ONLY(int res =) ::close(m_pipefd[1]);
    Dout(dc::finish|cond_error_cf(res == -1), res);
  }

  void test_close_input_device_closes_fd(bool started);
  void test_close_closes_input_fd(bool started);
  void test_disable_input_device(bool started, bool close);

  static boost::intrusive_ptr<TestInputDevice> create(bool start_it, bool dont_close = false)
  {
    DoutEntering(dc::notice, "TestInputDevice::create(" << start_it << ", " << dont_close << ")");
    auto device = evio::create<TestInputDevice>();
    int fd = device->m_pipefd[0];
    device->init(fd);
    if (dont_close)
      device->set_dont_close();
    device->tell_testsuite_fd(fd);
    if (start_it)
      device->start_input_device(state_t::wat(device->m_state));
    return device;
  }
};

class TestOutputDevice : public NoEpollOutputDevice, public FileDescriptor
{
 private:
  evio::OutputStream m_output;
  int m_pipefd[2];

 public:
  TestOutputDevice()
  {
    set_source(m_output);
    // Get some valid fd.
    if (pipe2(m_pipefd, O_CLOEXEC) == -1)
      DoutFatal(dc::core|error_cf, "pipe2(" << m_pipefd << ", O_CLOEXEC) = -1");
  }
  ~TestOutputDevice()
  {
    Dout(dc::system|continued_cf, "close(" << m_pipefd[1] << ") = ");
    CWDEBUG_ONLY(int res =) ::close(m_pipefd[0]);
    Dout(dc::finish|cond_error_cf(res == -1), res);
  }

  void test_close_output_device_closes_fd(bool started);
  void test_close_closes_output_fd(bool started);
  void test_disable_output_device(bool started, bool close);

  static boost::intrusive_ptr<TestOutputDevice> create(bool start_it, bool dont_close = false)
  {
    DoutEntering(dc::notice, "TestOutputDevice::create(" << start_it << ", " << dont_close << ")");
    auto device = evio::create<TestOutputDevice>();
    int fd = device->m_pipefd[1];
    device->init(fd);
    if (dont_close)
      device->set_dont_close();
    int val = fcntl(fd, F_GETFL);
    ASSERT(val != -1);
    EXPECT_TRUE(!!(val & O_NONBLOCK));  // Should be set to non-blocking after call to init().
    device->tell_testsuite_fd(fd);
    if (start_it)
      device->start_output_device();
    return device;
  }
};

//=============================================================================
// Test Fixtures
//

#include "EventLoopFixture.h"

class TestDestruction : public EventLoopFixture<testing::Test>
{
 protected:
  void SetUp() override
  {
#ifdef CWDEBUG
    Dout(dc::notice, "v TestDestruction::SetUp()");
    debug::Mark setup;
#endif
    EventLoopFixture<testing::Test>::SetUp();
    CALL(FileDescriptor::reset_instance_count());
  }

  void TearDown() override
  {
    Dout(dc::notice, "v TestDestruction::TearDown()");
    EventLoopFixture<testing::Test>::TearDown();
    CALL(FileDescriptor::test_all_instances_were_destructed());
  }
};

template<bool started, bool dontclose>
struct InputDeviceWithInit : public TestDestruction
{
  void test_body();
};

template<bool started, bool dontclose>
struct OutputDeviceWithInit : public TestDestruction
{
  void test_body();
};

template<bool started, bool close>
struct DisableInputDevice : public TestDestruction
{
  void test_body();
};

template<bool started, bool close>
struct DisableOutputDevice : public TestDestruction
{
  void test_body();
};

using InputDeviceWithInitFalseFalse = InputDeviceWithInit<false, false>;
using InputDeviceWithInitTrueFalse = InputDeviceWithInit<true, false>;
using InputDeviceWithInitFalseTrue = InputDeviceWithInit<false, true>;
using InputDeviceWithInitTrueTrue = InputDeviceWithInit<true, true>;

using OutputDeviceWithInitFalseFalse = OutputDeviceWithInit<false, false>;
using OutputDeviceWithInitFalseTrue = OutputDeviceWithInit<false, true>;
using OutputDeviceWithInitTrueFalse = OutputDeviceWithInit<true, false>;
using OutputDeviceWithInitTrueTrue = OutputDeviceWithInit<true, true>;

using DisableInputDeviceFalseFalse = DisableInputDevice<false, false>;
using DisableInputDeviceFalseTrue = DisableInputDevice<false, true>;
using DisableInputDeviceTrueFalse = DisableInputDevice<true, false>;
using DisableInputDeviceTrueTrue = DisableInputDevice<true, true>;

using DisableOutputDeviceFalseFalse = DisableOutputDevice<false, false>;
using DisableOutputDeviceFalseTrue = DisableOutputDevice<false, true>;
using DisableOutputDeviceTrueFalse = DisableOutputDevice<true, false>;
using DisableOutputDeviceTrueTrue = DisableOutputDevice<true, true>;

//=============================================================================

//-----------------------------------------------------------------------------
// FileDescriptor.is_valid

static int open_a_fd(int type)
{
  static int pipefd[2];
  int fd = -1;
  char temp_filename[37] = "/tmp/evio_test_XXXXXX_FileDescriptor";
  switch (type)
  {
    case 0:
    case 1:
      fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
      break;
    case 2:
    case 3:
      fd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
      break;
    case 4:
    case 5:
      fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
      break;
    case 6:
      fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
      break;
    case 7:
      fd = epoll_create1(EPOLL_CLOEXEC);
      break;
    case 8:
      fd = ::open("/proc/self/limits", O_RDONLY);
      break;
    case 9:
      fd = mkostemps(temp_filename, 15, O_CLOEXEC);
      break;
    case 10:
      if (pipe2(pipefd, O_NONBLOCK | O_CLOEXEC) == 0)
        fd = pipefd[0];
      break;
    case 11:
      fd = pipefd[1];
      break;
  }
  int ret = 0;
  if (type < 6 && (type & 1) == 1)
    ret = ::listen(fd, 4);
  if (fd < 0 || ret != 0)
  {
    // All of these must succeed.
    DoutFatal(dc::core|error_cf, "open_a_fd(" << type << ") == " << fd);
  }
  return fd;
}

TEST(FileDescriptor, is_valid)
{
  EXPECT_TRUE(evio::is_valid(STDIN_FILENO));
  EXPECT_TRUE(evio::is_valid(STDOUT_FILENO));
  EXPECT_TRUE(evio::is_valid(STDERR_FILENO));

  // Count the current number of valid fds under 1024.
  int valid_fds_before = 0;
  for (int fd = 0; fd < 1024; ++fd)
  {
    if (evio::is_valid(fd))
      ++valid_fds_before;
  }
  // We opened not that many fds.
  ASSERT_LT(valid_fds_before, 512);

  std::vector<int> fds;
  int valid_fds_expected = valid_fds_before;
  for (int type = 0; type < 12; ++type)
  {
    // Open a new fd.
    int fd1 = open_a_fd(type);
    // On linux, newly created file descriptors are minimal -- besides, a value larger than 1024 would get into trouble
    // with a standard library function call like select().
    EXPECT_LT(fd1, 1024);

    // Count the number of valid fds under 1024 again.
    int valid_fds_after = 0;
    for (int fd = 0; fd < 1024; ++fd)
    {
      if (evio::is_valid(fd))
        ++valid_fds_after;
    }
    if (type < 10)
      // There should be precisely one valid fd more now.
      ++valid_fds_expected;
    else if (type == 10)
      valid_fds_expected += 2;  // pipe(2).

    EXPECT_EQ(valid_fds_expected, valid_fds_after); // type 10 opened a pipe, with two ends.
    fds.push_back(fd1);
  }

  // Close all created file descriptors.
  for (int fd : fds)
  {
    EXPECT_TRUE(evio::is_valid(fd));
    EXPECT_EQ(close(fd), 0);
    EXPECT_FALSE(evio::is_valid(fd));
  }
}

//-----------------------------------------------------------------------------
// FileDescriptor.DefaultConstructor

TEST(FileDescriptor, DefaultConstructor)
{
  CALL(FileDescriptor::reset_instance_count());
  {
    // FileDescriptor is derived from utils::AIRefCount and therefore
    // must be created with new and assigned to a boost::intrusive_ptr.
    auto fd1 = evio::create<FileDescriptor>();
    EXPECT_TRUE((std::is_same<decltype(fd1), boost::intrusive_ptr<FileDescriptor>>::value));

    // Test all public member functions, for a default constructed FileDescriptor.
    EXPECT_FALSE(fd1->get_flags().is_output_device());
    EXPECT_FALSE(fd1->get_flags().is_input_device());
    EXPECT_FALSE(fd1->get_flags().is_writable());
    EXPECT_FALSE(fd1->get_flags().is_readable());
    EXPECT_FALSE(fd1->get_flags().dont_close());
    EXPECT_FALSE(fd1->get_flags().is_open());
    EXPECT_FALSE(fd1->get_flags().is_w_open());
    EXPECT_FALSE(fd1->get_flags().is_r_open());
    EXPECT_FALSE(fd1->get_flags().is_dead());
    EXPECT_FALSE(fd1->get_flags().is_disabled());
    EXPECT_FALSE(fd1->get_flags().is_r_disabled());
    EXPECT_FALSE(fd1->get_flags().is_w_disabled());
    EXPECT_FALSE(fd1->get_flags().is_debug_channel());

    // Test protected member functions.

    CALL(fd1->test_fd_is_minus_one());
    CALL(fd1->test_close_input_device_does_nothing());
    CALL(fd1->test_close_output_device_does_nothing());

    // Same here, because it just calls both. However, because it is
    // kinda dirty to call close twice on a row; lets create a new
    // object first.
    auto fd2 = evio::create<FileDescriptor>();
    CALL(fd2->test_close_does_nothing());
  }
  CALL(FileDescriptor::test_all_instances_were_destructed());
}

//-----------------------------------------------------------------------------
// FileDescriptor.InputDeviceNoInit

TEST(FileDescriptor, InputDeviceNoInit)
{
  CALL(FileDescriptor::reset_instance_count());
  {
    auto fd1 = evio::create<TestInputDevice>();
    EXPECT_TRUE((std::is_same<decltype(fd1), boost::intrusive_ptr<TestInputDevice>>::value));

    // Test all public member functions, for a default constructed FileDescriptor.
    EXPECT_TRUE(fd1->get_flags().is_input_device());

    EXPECT_FALSE(fd1->get_flags().is_output_device());
    EXPECT_FALSE(fd1->get_flags().is_writable());
    EXPECT_FALSE(fd1->get_flags().is_readable());   // init() wasn't called.
    EXPECT_FALSE(fd1->get_flags().dont_close());
    EXPECT_FALSE(fd1->get_flags().is_open());       // init() wasn't called.
    EXPECT_FALSE(fd1->get_flags().is_w_open());
    EXPECT_FALSE(fd1->get_flags().is_r_open());     // init() wasn't called.
    EXPECT_FALSE(fd1->get_flags().is_dead());
    EXPECT_FALSE(fd1->get_flags().is_disabled());
    EXPECT_FALSE(fd1->get_flags().is_debug_channel());

    // Test protected member functions.

    CALL(fd1->test_fd_is_minus_one());
    CALL(fd1->test_close_input_device_does_nothing());
    CALL(fd1->test_close_output_device_does_nothing());

    // Same here, because it just calls both. However, because it is
    // kinda dirty to call close twice on a row; lets create a new
    // object first.
    auto fd2 = evio::create<FileDescriptor>();
    CALL(fd2->test_close_does_nothing());
  }
  CALL(FileDescriptor::test_all_instances_were_destructed());
}

//-----------------------------------------------------------------------------
// FileDescriptor.OutputDeviceNoInit

TEST(FileDescriptor, OutputDeviceNoInit)
{
  CALL(FileDescriptor::reset_instance_count());
  {
    auto fd1 = evio::create<TestOutputDevice>();
    EXPECT_TRUE((std::is_same<decltype(fd1), boost::intrusive_ptr<TestOutputDevice>>::value));

    // Test all public member functions, for a default constructed FileDescriptor.
    EXPECT_TRUE(fd1->get_flags().is_output_device());

    EXPECT_FALSE(fd1->get_flags().is_input_device());
    EXPECT_FALSE(fd1->get_flags().is_writable());   // init() wasn't called.
    EXPECT_FALSE(fd1->get_flags().is_readable());
    EXPECT_FALSE(fd1->get_flags().dont_close());
    EXPECT_FALSE(fd1->get_flags().is_open());       // init() wasn't called.
    EXPECT_FALSE(fd1->get_flags().is_w_open());     // init() wasn't called.
    EXPECT_FALSE(fd1->get_flags().is_r_open());
    EXPECT_FALSE(fd1->get_flags().is_dead());
    EXPECT_FALSE(fd1->get_flags().is_disabled());
    EXPECT_FALSE(fd1->get_flags().is_debug_channel());

    // Test protected member functions.

    CALL(fd1->test_fd_is_minus_one());
    CALL(fd1->test_close_input_device_does_nothing());
    CALL(fd1->test_close_output_device_does_nothing());

    // Same here, because it just calls both. However, because it is
    // kinda dirty to call close twice on a row; lets create a new
    // object first.
    auto fd2 = evio::create<FileDescriptor>();
    CALL(fd2->test_close_does_nothing());
  }
  CALL(FileDescriptor::test_all_instances_were_destructed());
}

//-----------------------------------------------------------------------------
// InputDeviceWithInitFalseFalse.NotStartedNoDontClose
// InputDeviceWithInitTrueFalse.StartedNoDontClose
// InputDeviceWithInitFalseTrue.NotStartedDontClose
// InputDeviceWithInitTrueTrue.StartedDontClose

TEST_F(InputDeviceWithInitFalseFalse, NotStartedNoDontClose) { CALL(test_body()); }
TEST_F(InputDeviceWithInitTrueFalse, StartedNoDontClose) { CALL(test_body()); }
TEST_F(InputDeviceWithInitFalseTrue, NotStartedDontClose) { CALL(test_body()); }
TEST_F(InputDeviceWithInitTrueTrue, StartedDontClose) { CALL(test_body()); }

template<bool started, bool dontclose>
void InputDeviceWithInit<started, dontclose>::test_body()
{
  // With and without starting the device.
  // The difference is that if it isn't started than there is no
  // need for a call to allow_deletion() upon closing the device,
  // as it is not stopped as part of that.
  auto fd1 = TestInputDevice::create(started, dontclose);

  // Test all public member functions, for a default constructed FileDescriptor.
  EXPECT_TRUE(fd1->get_flags().is_input_device());
  EXPECT_TRUE(fd1->get_flags().is_readable());
  EXPECT_TRUE(fd1->get_flags().is_open());
  EXPECT_TRUE(fd1->get_flags().is_r_open());

  EXPECT_FALSE(fd1->get_flags().is_output_device());
  EXPECT_FALSE(fd1->get_flags().is_writable());
  EXPECT_EQ(fd1->get_flags().dont_close(), dontclose);
  EXPECT_FALSE(fd1->get_flags().is_w_open());
  EXPECT_FALSE(fd1->get_flags().is_dead());
  EXPECT_FALSE(fd1->get_flags().is_disabled());
#ifdef CWDEBUG
  EXPECT_FALSE(fd1->get_flags().is_debug_channel());
#endif

  // Test protected member functions.

  CALL(fd1->test_fd_is_set());
  CALL(fd1->test_close_output_device_does_nothing());
  CALL(fd1->test_close_input_device_closes_fd(started));

  EXPECT_TRUE(fd1->get_flags().is_input_device());
  EXPECT_TRUE(fd1->get_flags().is_dead());

  EXPECT_FALSE(fd1->get_flags().is_output_device());
  EXPECT_FALSE(fd1->get_flags().is_writable());
  EXPECT_FALSE(fd1->get_flags().is_readable());
  EXPECT_EQ(fd1->get_flags().dont_close(), dontclose);
  EXPECT_FALSE(fd1->get_flags().is_open());
  EXPECT_FALSE(fd1->get_flags().is_w_open());
  EXPECT_FALSE(fd1->get_flags().is_r_open());
  EXPECT_FALSE(fd1->get_flags().is_disabled());
#ifdef CWDEBUG
  EXPECT_FALSE(fd1->get_flags().is_debug_channel());
#endif

  auto fd2 = TestInputDevice::create(started, dontclose);
  CALL(fd2->test_close_closes_input_fd(started));
}

void TestInputDevice::test_close_input_device_closes_fd(bool started)
{
  ASSERT(evio::is_valid(get_fd()) && get_flags().is_readable());        // Preconditions for this test.
  m_closed_called  = false;
  RefCountReleaser rcr = close_input_device();
  EXPECT_TRUE(m_closed_called);                 // closed() was called.
  EXPECT_EQ(rcr, started);                      // If nad_rcr is true then going out of scope will call allow_deletion(),
                                                // but that is obviously only needed when the device was inhibited for deletion because it was started.
}

void TestInputDevice::test_close_closes_input_fd(bool started)
{
  ASSERT(evio::is_valid(get_fd()) && get_flags().is_readable());        // Preconditions for this test.
  m_closed_called  = false;
  RefCountReleaser rcr = close();
  EXPECT_TRUE(m_closed_called);       // closed() was called.
  EXPECT_EQ(rcr, started);
}

//-----------------------------------------------------------------------------
// OutputDeviceWithInitFalseFalse.NotStartedNoDontClose
// OutputDeviceWithInitTrueFalse.StartedNoDontClose
// OutputDeviceWithInitFalseTrue.NotStartedDontClose
// OutputDeviceWithInitTrueTrue.StartedDontClose

TEST_F(OutputDeviceWithInitFalseFalse, NotStartedNoDontClose) { CALL(test_body()); }
TEST_F(OutputDeviceWithInitTrueFalse, StartedNoDontClose) { CALL(test_body()); }
TEST_F(OutputDeviceWithInitFalseTrue, NotStartedDontClose) { CALL(test_body()); }
TEST_F(OutputDeviceWithInitTrueTrue, StartedDontClose) { CALL(test_body()); }

template<bool started, bool dontclose>
void OutputDeviceWithInit<started, dontclose>::test_body()
{
  auto fd1 = TestOutputDevice::create(started, dontclose);

  // Test all public member functions, for a default constructed FileDescriptor.
  EXPECT_TRUE(fd1->get_flags().is_output_device());
  EXPECT_TRUE(fd1->get_flags().is_writable());
  EXPECT_TRUE(fd1->get_flags().is_open());
  EXPECT_TRUE(fd1->get_flags().is_w_open());

  EXPECT_FALSE(fd1->get_flags().is_input_device());
  EXPECT_FALSE(fd1->get_flags().is_readable());
  EXPECT_EQ(fd1->get_flags().dont_close(), dontclose);
  EXPECT_FALSE(fd1->get_flags().is_r_open());
  EXPECT_FALSE(fd1->get_flags().is_dead());
  EXPECT_FALSE(fd1->get_flags().is_disabled());
#ifdef CWDEBUG
  EXPECT_FALSE(fd1->get_flags().is_debug_channel());
#endif

  // Test protected member functions.

  CALL(fd1->test_fd_is_set());
  CALL(fd1->test_close_input_device_does_nothing());
  CALL(fd1->test_close_output_device_closes_fd(started));

  EXPECT_TRUE(fd1->get_flags().is_output_device());
  EXPECT_TRUE(fd1->get_flags().is_dead());

  EXPECT_FALSE(fd1->get_flags().is_input_device());
  EXPECT_FALSE(fd1->get_flags().is_writable());
  EXPECT_FALSE(fd1->get_flags().is_readable());
  EXPECT_EQ(fd1->get_flags().dont_close(), dontclose);
  EXPECT_FALSE(fd1->get_flags().is_open());
  EXPECT_FALSE(fd1->get_flags().is_w_open());
  EXPECT_FALSE(fd1->get_flags().is_r_open());
  EXPECT_FALSE(fd1->get_flags().is_disabled());
#ifdef CWDEBUG
  EXPECT_FALSE(fd1->get_flags().is_debug_channel());
#endif

  auto fd2 = TestOutputDevice::create(started, dontclose);
  CALL(fd2->test_close_closes_output_fd(started));
}

void TestOutputDevice::test_close_output_device_closes_fd(bool started)
{
  ASSERT(evio::is_valid(get_fd()) && get_flags().is_writable());       // Preconditions for this test.
  m_closed_called  = false;
  RefCountReleaser rcr = close_output_device();
  EXPECT_TRUE(m_closed_called);         // closed() was called.
  EXPECT_EQ(rcr, started);              // If nad_rcr is true then going out of scope will call allow_deletion(),
                                        // but that is obviously only needed when the device was inhibited for deletion because it was started.
}

void TestOutputDevice::test_close_closes_output_fd(bool started)
{
  ASSERT(evio::is_valid(get_fd()) && get_flags().is_writable());     // Preconditions for this test.
  m_closed_called  = false;
  RefCountReleaser rcr = close();
  EXPECT_TRUE(m_closed_called);       // closed() was called.
  EXPECT_EQ(rcr, started);
}

//-----------------------------------------------------------------------------
// DisableInputDeviceFalseFalse.NotStartedNoClose
// DisableInputDeviceFalseTrue.NotStartedClose
// DisableInputDeviceTrueFalse.StartedNoClose
// DisableInputDeviceTrueTrue.StartedClose

TEST_F(DisableInputDeviceFalseFalse, NotStartedNoClose) { CALL(test_body()); }
TEST_F(DisableInputDeviceFalseTrue, NotStartedClose) { CALL(test_body()); }
TEST_F(DisableInputDeviceTrueFalse, StartedNoClose) { CALL(test_body()); }
TEST_F(DisableInputDeviceTrueTrue, StartedClose) { CALL(test_body()); }

template<bool started, bool close>
void DisableInputDevice<started, close>::test_body()
{
  auto fd1 = TestInputDevice::create(started);
  CALL(fd1->test_disable_input_device(started, close));
}

void TestInputDevice::test_disable_input_device(bool started, bool close)
{
  EXPECT_EQ(get_flags().is_active_input_device(), started);
  disable_input_device();
  EXPECT_TRUE(get_flags().is_disabled());
  EXPECT_TRUE(get_flags().is_r_disabled());
  EXPECT_FALSE(get_flags().is_w_disabled());
  EXPECT_FALSE(get_flags().is_active_input_device());
  if (close)
    close_input_device();
  enable_input_device();
  EXPECT_FALSE(get_flags().is_disabled());
  EXPECT_FALSE(get_flags().is_r_disabled());
  EXPECT_EQ(get_flags().is_active_input_device(), !close);      // Even when not started before, enable_input_device() will start
                                                                // an input device with a valid fd (because data might be readable).
  // While a device is added it will not be deleted.
  // In order to let TestDestruction::TearDown() not fail, call close_input_device() here.
  // This is also allowed when the device is already closed.
  RefCountReleaser rcr = close_input_device();
  EXPECT_EQ(rcr, !close);
}

//-----------------------------------------------------------------------------
// DisableOutputDeviceFalseFalse.NotStartedNoClose
// DisableOutputDeviceFalseTrue.NotStartedClose
// DisableOutputDeviceTrueFalse.StartedNoClose
// DisableOutputDeviceTrueTrue.StartedClose

TEST_F(DisableOutputDeviceFalseFalse, NotStartedNoClose) { CALL(test_body()); }
TEST_F(DisableOutputDeviceFalseTrue, NotStartedClose) { CALL(test_body()); }
TEST_F(DisableOutputDeviceTrueFalse, StartedNoClose) { CALL(test_body()); }
TEST_F(DisableOutputDeviceTrueTrue, StartedClose) { CALL(test_body()); }

template<bool started, bool close>
void DisableOutputDevice<started, close>::test_body()
{
  auto fd1 = TestOutputDevice::create(started);
  CALL(fd1->test_disable_output_device(started, close));
}

void TestOutputDevice::test_disable_output_device(bool started, bool close)
{
  EXPECT_EQ(get_flags().is_active_output_device(), started);
  disable_output_device();
  EXPECT_TRUE(get_flags().is_disabled());
  EXPECT_TRUE(get_flags().is_w_disabled());
  EXPECT_FALSE(get_flags().is_r_disabled());
  EXPECT_FALSE(get_flags().is_active_output_device());
  if (close)
    close_output_device();
  enable_output_device();
  EXPECT_FALSE(get_flags().is_disabled());
  EXPECT_FALSE(get_flags().is_w_disabled());
  // Currently, calling enable_output_device() always calls start_output_device(),
  // even when there is nothing to write.
  EXPECT_EQ(get_flags().is_active_output_device(), !close);
  // While a device is aded it will not be deleted.
  // In order to let TestDestruction::TearDown() not fail, call close_output_device() here.
  // This is also allowed when the device is already closed.
  RefCountReleaser rcr = close_output_device();
  EXPECT_EQ(rcr, !close);
}

//-----------------------------------------------------------------------------
