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
  void read_from_fd(int& allow_deletion_count, int fd) override;

 public:
  void init(int fd, bool make_fd_non_blocking)
  {
    fd_init(fd, make_fd_non_blocking);
  }

  void set_dont_close()
  {
    state_t::wat(m_state)->m_flags.set_dont_close();
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
  void write_to_fd(int& allow_deletion_count, int fd) override;

 public:
  void init(int fd, bool make_fd_non_blocking)
  {
    fd_init(fd, make_fd_non_blocking);
  }

  void set_dont_close()
  {
    state_t::wat(m_state)->m_flags.set_dont_close();
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

    fdp0->init(0, false);        // Standard input.
    fdp0->set_dont_close();
    fdp1->init(1, false);        // Standard output.
    fdp1->set_dont_close();
    fdp1->close_on_exit();

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
void TestInputDevice::read_from_fd(int& allow_deletion_count, int fd)
{
  DoutEntering(dc::notice, "TestInputDevice::read_from_fd({" << allow_deletion_count << "}, " << fd << ") [" << this << "]");

  char buf[256];
  ssize_t len;
  do
  {
    len = ::read(fd, buf, 256);
    Dout(dc::notice, "Read: \"" << libcwd::buf2str(buf, len) << "\".");
  }
  while (len == 256);

  if (strncmp(buf, "quit\n", 5) == 0)
    close(allow_deletion_count); // Remove this object; this will cause the main event loop to terminate.
}

// Write thread.
void TestOutputDevice::write_to_fd(int& allow_deletion_count, int fd)
{
  DoutEntering(dc::notice, "TestOutputDevice::write_to_fd({" << allow_deletion_count << "}, " << fd << ") [" << this << "]");
  [[maybe_unused]] int unused = ::write(fd, "Hello World\n", 12);
  stop_output_device(allow_deletion_count);
}
