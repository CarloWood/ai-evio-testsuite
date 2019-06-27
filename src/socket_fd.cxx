// Run this test while './http_server' from the project https://github.com/CarloWood/curl_http_pipeline_tester.git is already running.

#include "sys.h"
#include "debug.h"
#include "threadpool/AIThreadPool.h"
#include "evio/InputDevice.h"
#include "evio/OutputDevice.h"
#include "evio/EventLoopThread.h"
#include "evio/inet_support.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"

#include <sstream>
#include <cstring>	// Needed for memset.
#include <cerrno>

#include <netdb.h>      // Needed for gethostbyname() */
#include <sys/time.h>
#include <sys/types.h>  // Needed for socket, send etc.
#include <sys/socket.h> // Needed for socket, send etc.
#include <netinet/in.h> // Needed for htons.

#ifdef CWDEBUG
#include <libcwd/buf2str.h>
#endif

class Socket : public evio::InputDevice, public evio::OutputDevice
{
 public:
  struct VT_type : InputDevice::VT_type, OutputDevice::VT_type
  {
  };

  struct VT_impl : InputDevice::VT_impl, OutputDevice::VT_impl
  {
    static NAD_DECL(read_from_fd, evio::InputDevice* _self, int fd); // Read thread.
    static NAD_DECL(write_to_fd, evio::OutputDevice* _self, int fd); // Write thread.

    static constexpr VT_type VT{
      /*InputDevice*/
    { nullptr,
      read_from_fd,
      hup,
      exceptional,
      read_returned_zero,
      read_error,
      data_received },
      /*OutputDevice*/
    { nullptr,
      write_to_fd,
      write_error }
    };
  };

  // Make a deep copy of VT_ptr.
  VT_type* clone_VT() override { return VT_ptr.clone(this); }

  utils::VTPtr<Socket, InputDevice, OutputDevice> VT_ptr;

 private:
  int m_request;

 public:
  Socket() : VT_ptr(this), m_request(0) { }

  void connect_to_server(char const* remote_host, int remote_port);
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
    // Initialize the IO event loop thread.
    evio::EventLoop event_loop(low_priority_handler);
    boost::intrusive_ptr<Socket> fdp0 = new Socket;
    fdp0->connect_to_server("localhost", 9001);
    event_loop.join();
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}

void Socket::connect_to_server(char const* remote_host, int remote_port)
{
  // Get host by name.
  struct hostent* hp;
  if (!(hp = gethostbyname(remote_host)))
  {
    herror("gethostbyname");
    exit(-1);
  }
  // Dump the info we got.
  if (evio::print_hostent_on(hp, std::cout))
    exit(-1);

  struct sockaddr_in remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));
  remote_addr.sin_family = AF_INET;
  remote_addr.sin_port = htons(remote_port);
  memcpy((char*)&remote_addr.sin_addr, (char*)hp->h_addr_list[0], sizeof(struct in_addr));

  Dout(dc::notice, "Connecting to port " << remote_port);

  // Create socket to remote host.
  int fd_remote;
  if ((fd_remote = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) == -1)
  {
    perror("socket");
    exit(-1);
  }

  int opt = 512;
  if (setsockopt(fd_remote, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt)) < 0)
  {
    perror("setsockopt");
    exit(-1);
  }
  opt = 512;
  if (setsockopt(fd_remote, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt)) < 0)
  {
    perror("setsockopt");
    exit(-1);
  }

  // Connect the socket.
  if (connect(fd_remote, (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0)
  {
    int err = errno;
    perror("connect");
    if (err != EINPROGRESS)
      exit(-1);
    Dout(dc::notice, "\"Connect in progress\".");
  }
  else
    Dout(dc::notice, "\"Connected\".");

  init(fd_remote);
  state_t::wat state_w(m_state);
  // This class does not use input/output buffers but directly overrides read_from_fd and write_to_fd.
  // Therefore it is not necessary to call set_sink() and set_source().
  start_input_device(state_w);
  start_output_device(state_w);
}

// Read thread.
NAD_DECL(Socket::VT_impl::read_from_fd, evio::InputDevice* _self, int fd)
{
  DoutEntering(dc::notice, "Socket::read_from_fd(" NAD_DoutEntering_ARG << fd << ")");
  Socket* self = static_cast<Socket*>(_self);
  char buf[256];
  ssize_t len;
  do
  {
    Dout(dc::system|continued_cf, "read(" << fd << ", char[256], 256) = ");
    len = ::read(fd, buf, 256);
    Dout(dc::finish|cond_error_cf(len == -1), len);
    if (len == -1)
      NAD_CALL(self->evio::InputDevice::close);
    Dout(dc::notice, "Read: \"" << libcwd::buf2str(buf, len) << "\".");
  }
  while (len == 256);
  if (strncmp(buf + len - 17, "#5</body></html>\n", 17) == 0)
  {
    self->stop_input_device();
    evio::EventLoopThread::instance().stop_running();
  }
}

// Write thread.
NAD_DECL(Socket::VT_impl::write_to_fd, evio::OutputDevice* _self, int fd)
{
  Socket* self = static_cast<Socket*>(_self);
  DoutEntering(dc::notice, "Socket::write_to_fd(" NAD_DoutEntering_ARG << fd << ")");
  if (self->m_request < 6)
  {
    std::stringstream ss;
    ss << "GET / HTTP/1.1\r\nHost: localhost:9001\r\nAccept: */*\r\nX-Request: " << self->m_request << "\r\nX-Sleep: " << (200 * self->m_request) << "\r\n\r\n";
    ++self->m_request;
    [[maybe_unused]] int unused = ::write(fd, ss.str().data(), ss.str().length());
    Dout(dc::notice, "Wrote \"" << libcwd::buf2str(ss.str().data(), ss.str().length()) << "\".");
  }
  else
    NAD_CALL(self->stop_output_device);
}
