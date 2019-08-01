#include "sys.h"
#include "evio/InputDevice.h"
#include "evio/OutputDevice.h"
#include "debug.h"
#include "evio/EventLoop.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

using namespace evio;

class TestInputDevice : public InputDevice
{
 public:
  using VT_type = InputDevice::VT_type;
  #define VT_TestInputDevice VT_evio_InputDevice

  struct VT_impl : InputDevice::VT_impl
  {
    static void read_from_fd(int& allow_deletion_count, InputDevice* self, int fd); // Read thread.

    // Virtual table of TestInputDevice.
    static constexpr VT_type VT VT_TestInputDevice;
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<TestInputDevice, InputDevice> VT_ptr;

  TestInputDevice() : VT_ptr(this) { }

 public:
  void init(int fd)
  {
    FileDescriptor::init(fd);
  }

  void start()
  {
    // This object does not use a buffer, but instead overrides read_from_fd directly.
    // Therefore it is not necessary to call set_sink().
    start_input_device(state_t::wat(m_state));
  }
};

class TestOutputDevice : public OutputDevice
{
 public:
  using VT_type = OutputDevice::VT_type;
  #define VT_TestOutputDevice VT_evio_OutputDevice

  struct VT_impl : OutputDevice::VT_impl
  {
    static void write_to_fd(int& allow_deletion_count, OutputDevice* self, int fd);

    static constexpr VT_type VT VT_TestOutputDevice;
  };

  // Make a deep copy of VT_ptr.
  VT_type* clone_VT() override { return VT_ptr.clone(this); }

  utils::VTPtr<TestOutputDevice, OutputDevice> VT_ptr;

 public:
  TestOutputDevice() : VT_ptr(this) { }

  void init(int fd)
  {
    FileDescriptor::init(fd);
  }

  void start()
  {
    // This object does not use a buffer, but instead overrides write_to_fd directly.
    // Therefore it is not necessary to call set_source().
    start_output_device(state_t::wat(m_state));
  }
};

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  AIThreadPool thread_pool;
  [[maybe_unused]] AIQueueHandle high_priority_handler = thread_pool.new_queue(32);
  [[maybe_unused]] AIQueueHandle medium_priority_handler = thread_pool.new_queue(32);
  AIQueueHandle low_priority_handler = thread_pool.new_queue(16);

  try
  {
    EventLoop event_loop(low_priority_handler);

    auto fdp0 = evio::create<TestInputDevice>();
    auto fdp1 = evio::create<TestOutputDevice>();

    fdp0->init(0);        // Standard input.
    fdp1->init(1);        // Standard output.

    fdp0->start();
    fdp1->start();

    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}

// Read thread.
void TestInputDevice::VT_impl::read_from_fd(int& allow_deletion_count, evio::InputDevice* _self, int fd)
{
  DoutEntering(dc::notice, "TestInputDevice::read_from_fd({" << allow_deletion_count << "}, " << fd << ")");

  char buf[256];
  ssize_t len;
  do
  {
    len = ::read(fd, buf, 256);
    Dout(dc::notice, "Read: \"" << libcwd::buf2str(buf, len) << "\".");
  }
  while (len == 256);

  if (strncmp(buf, "quit\n", 5) == 0)
  {
    TestInputDevice* self = static_cast<TestInputDevice*>(_self);
    EventLoopThread::instance().stop_running(); // Terminate EventLoopThread.
    self->close(allow_deletion_count); // Remove this object.
  }
}

// Write thread.
void TestOutputDevice::VT_impl::write_to_fd(int& allow_deletion_count, OutputDevice* _self, int fd)
{
  TestOutputDevice* self = static_cast<TestOutputDevice*>(_self);
  DoutEntering(dc::notice, "TestOutputDevice::write_to_fd({" << allow_deletion_count << "}, " << fd << ")");
  [[maybe_unused]] int unused = ::write(fd, "Hello World\n", 12);
  self->stop_output_device(allow_deletion_count);
}
