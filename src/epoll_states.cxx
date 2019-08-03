#include <thread>
#include <mutex>
#include <iostream>
#include <functional>
#include <condition_variable>
#include <cassert>
#include <cstring>
#include <csignal>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

std::mutex cout_mutex;

//----------------------------------------------------------------------------
// Thread
//

class PermutationFailure : public std::runtime_error
{
 public:
  PermutationFailure(char const* msg) : std::runtime_error(msg) { }
};

namespace thread_permuter {

enum state_type
{
  yielding,     // Just yielding.
  blocking,     // This thread should not be run anymore until at least one other thread did run.
  failed,       // This thread encountered an error condition and threw an exception.
  finished      // Returned from m_test().
};

class Thread
{
 public:
  Thread(std::function<void()> test);

  void start(char thread_name);         // Start the thread and prepare calling step().
  void run();                           // Entry point of m_thread.
  state_type step();                    // Wake up the thread and let it run till the next check point (or finish).
                                        // Returns true when m_test() returned.
  void pause(state_type state, bool no_wait = false); // Pause the thread and wake up the main thread again.
  void stop();                          // Called when all permutation have been run.

  char get_name() const { return m_thread_name; }
  std::string const& what() const { return m_what; }

 protected:
  std::function<void()> m_test;         // Thread entry point. The first time step() is called
                                        // after start(), this function will be called.
  std::thread m_thread;                 // The actual thread.
  state_type m_state;
  bool m_last_permutation;              // True after all permutation have been run.

  std::condition_variable m_paused_condition;
  std::mutex m_paused_mutex;
  bool m_paused;                        // True when the thread is waiting.
  std::string m_what;                   // Error of last exception thrown.

  char m_thread_name;                   // Used for debugging output; set by start().
  int m_signum;

  static thread_local Thread* tl_self;  // A thread_local pointer to self.

  static void signal_handler(int);

 public:
  static void yield() { tl_self->pause(yielding); }
  static void blocked() { tl_self->pause(blocking); }
  static void fail(char const* what) { tl_self->m_what = what; tl_self->pause(failed); }
  static char name() { return tl_self->get_name(); }
  void wakeup();
};

Thread::Thread(std::function<void()> test) :
  m_test(test), m_state(yielding),
  m_last_permutation(false), m_paused(false),
  m_thread_name('?'),
  m_signum(SIGRTMIN)
{
  struct sigaction action;
  std::memset(&action, 0, sizeof(struct sigaction));
  action.sa_handler = SIG_IGN;
  sigaction(m_signum, &action, NULL);
  sigset_t rt_signals;
  sigemptyset(&rt_signals);
  sigaddset(&rt_signals, m_signum);
  sigprocmask(SIG_BLOCK, &rt_signals, nullptr);
}

//static
void Thread::signal_handler(int)
{
}

void Thread::start(char thread_name)
{
  struct sigaction action;
  std::memset(&action, 0, sizeof(struct sigaction));
  action.sa_handler = &signal_handler;
  sigaction(m_signum, &action, NULL);
  sigset_t sigmask;
  sigemptyset(&sigmask);
  sigaddset(&sigmask, m_signum);
  sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
  m_thread_name = thread_name;
  std::unique_lock<std::mutex> lock(m_paused_mutex);
  // Start thread.
  m_thread = std::thread([this](){ Thread::run(); });
  // Wait until the thread is paused.
  m_paused_condition.wait(lock, [this]{ return m_paused; });
}

void Thread::run()
{
  tl_self = this;                       // Allow a checkpoint to find this object back.
  std::cout << get_name() << " EpollThread started." << std::endl;
  pause(yielding);                      // Wait until we may enter m_test() for the first time.
  m_test();                             // Call the test function.
  std::cout << get_name() << " EpollThread exiting." << std::endl;
}

void Thread::pause(state_type state, bool no_wait)
{
  //std::cout << "* pause()" << std::endl;
  m_state = state;
  std::unique_lock<std::mutex> lock(m_paused_mutex);
  m_paused = true;
  m_paused_condition.notify_one();
  if (!no_wait)
    m_paused_condition.wait(lock, [this]{ return !m_paused; });
}

state_type Thread::step()
{
  //std::cout << ">> step()" << std::endl;
  std::unique_lock<std::mutex> lock(m_paused_mutex);
  m_paused = false;
  // Wake up the thread.
  m_paused_condition.notify_one();
  // Wait until the thread is paused again.
  m_paused_condition.wait(lock, [this]{ return m_paused; });
  return m_state;
}

void Thread::stop()
{
  std::cout << ">> stop()" << std::endl;
  m_last_permutation = true;
  {
    std::unique_lock<std::mutex> lock(m_paused_mutex);
    m_paused = false;
    // Wake up the thread and let it exit.
    m_paused_condition.notify_one();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  wakeup();
  // Join with it.
  m_thread.join();
}

void Thread::wakeup()
{
  pthread_kill(m_thread.native_handle(), m_signum);
}

//static
thread_local Thread* Thread::tl_self;

} // namespace thread_permuter

#if defined(CWDEBUG) && !defined(DOXYGEN)
NAMESPACE_DEBUG_CHANNELS_START
channel_ct permutation("PERMUTATION");
NAMESPACE_DEBUG_CHANNELS_END
#endif
//----------------------------------------------------------------------------

using std::cout;
using std::endl;

#define AI_CASE_RETURN(x) do { case x: return #x; } while(0)

char const* epoll_event_str(uint32_t event)
{
  switch (event)
  {
    AI_CASE_RETURN(EPOLLIN);
    AI_CASE_RETURN(EPOLLOUT);
    AI_CASE_RETURN(EPOLLRDHUP);
    AI_CASE_RETURN(EPOLLPRI);
    AI_CASE_RETURN(EPOLLERR);
    AI_CASE_RETURN(EPOLLHUP);
    AI_CASE_RETURN(EPOLLET);
    AI_CASE_RETURN(EPOLLONESHOT);
    AI_CASE_RETURN(EPOLLWAKEUP);
    AI_CASE_RETURN(EPOLLEXCLUSIVE);
  }
  return "Unknown epoll event";
}

std::string events_to_str(uint32_t events)
{
  std::string result;
  char const* separator = "";
  for (uint32_t event = 1; event != 0; event <<= 1)
    if ((events & event))
    {
      result += separator;
      result += epoll_event_str(event);
      separator = "|";
    }
  return result;
}

struct Epoll
{
  int m_epoll_fd;
  int m_watched_fd;
  bool m_added;
  struct epoll_event m_current_events;

  Epoll(int watched_fd);

  void add_fd_with_events(uint32_t events)
  {
    cout << ">> Epoll::add_fd_with_events(" << events_to_str(events) << ")" << endl;
    assert(!m_added);
    assert(m_current_events.events == 0);
    m_current_events = { events|EPOLLET, { 0 } };
    cout << "\e[32mepoll_ctl(" << m_epoll_fd << ", EPOLL_CTL_ADD, " << m_watched_fd << ", " << events_to_str(m_current_events.events) << ")\e[0m" << endl;
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_watched_fd, &m_current_events);
    m_added = true;
  }

  void mod_fd(uint32_t new_events)
  {
    cout << ">> Epoll::mod_fd(" << events_to_str(new_events) << ")" << endl;
    assert(m_added);
    new_events |= EPOLLET;
    assert(m_current_events.events != new_events);
    m_current_events = { new_events, { 0 } };
    cout << "\e[32mepoll_ctl(" << m_epoll_fd << ", EPOLL_CTL_MOD, " << m_watched_fd << ", " << events_to_str(m_current_events.events) << ")\e[0m" << endl;
    epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, m_watched_fd, &m_current_events);
  }

  void del_fd()
  {
    cout << ">> Epoll::del_fd()" << endl;
    assert(m_added);
    cout << "\e[32mepoll_ctl(" << m_epoll_fd << ", EPOLL_CTL_DEL, " << m_watched_fd << ", nullptr)\e[0m" << endl;
    epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_watched_fd, nullptr);
    m_current_events = { 0, { 0 } };
    m_added = false;
  }
};

Epoll::Epoll(int watched_fd) : m_watched_fd(watched_fd), m_added(false), m_current_events{0, {0}}
{
  m_epoll_fd = epoll_create(1);
  cout << "\e[32m" << "epoll_create(1) = " << m_epoll_fd << "\e[0m" << endl;
}

struct Socket
{
  int m_client_fd;
  int m_accept_fd;
  size_t m_sent;
  size_t m_received;
  size_t m_written;
  size_t m_read;
  static char s_buffer[];

  Socket();

  void send(int n);
  void recv(int n);
  void write(int n);
  void read(int n);

  void close_send();
};

struct PipeReadEnd
{
  int m_pipefd[2];
  size_t m_sent;
  size_t m_read;
  static char s_buffer[];

  PipeReadEnd();

  void send(int n);
  void read(int n);

  void close_send();
};

PipeReadEnd::PipeReadEnd() : m_sent(0), m_read(0)
{
  pipe2(m_pipefd, O_NONBLOCK);
}

//static
char PipeReadEnd::s_buffer[1024 * 1024];

void PipeReadEnd::send(int n)
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> PipeReadEnd::send(" << n << ")" << endl;
  }
  assert(n < (int)sizeof(s_buffer));
  for (;;)
  {
    ssize_t ret;
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      cout << "\e[32mwrite(" << m_pipefd[1] << ", buffer, " << n << ") = ";
      ret = ::write(m_pipefd[1], s_buffer, n);
      cout << ret << "\e[0m";
      if (ret == -1)
        cout << " (" << std::strerror(errno) << ')';
      cout << endl;
    }
    if (ret == -1)
    {
      if (errno != EAGAIN)
      {
        perror("write");
        assert(ret >= 0);
      }
      break;
    }
    m_sent += ret;
    break;
  }
}

void PipeReadEnd::read(int n)
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> PipeReadEnd::read(" << n << ")" << endl;
  }
  assert(n < (int)sizeof(s_buffer));
  ssize_t ret = -1;
  while (ret == -1)
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << "\e[32mread(" << m_pipefd[0] << ", buffer, " << n << ") = ";
    ret = ::read(m_pipefd[0], s_buffer, n);
    cout << ret << "\e[0m" << endl;
  }
  if (ret == 0)
    cout << "  [EOF]" << endl;
  else
    m_read += ret;
}

void PipeReadEnd::close_send()
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> PipeReadEnd::close_send()" << endl;
  }
  ssize_t ret;
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << "\e[32mclose(" << m_pipefd[1] << ") = ";
    ret = ::close(m_pipefd[1]);
    cout << ret << "\e[0m";
    if (ret == -1)
      cout << " (" << std::strerror(errno) << ')';
    cout << endl;
  }
  assert(ret >= 0);
}

Socket::Socket() : m_sent(0), m_received(0), m_written(0), m_read(0)
{
  int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  int opt = 1;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  opt = 4096;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
  opt = 4096;
  ::setsockopt(listen_fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
  struct sockaddr_in bind_addr;
  std::memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(9002);
  ::bind(listen_fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr));
  ::listen(listen_fd, 4);
  m_client_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  cout << "Client socket: " << m_client_fd << endl;
  opt = 4096;
  ::setsockopt(m_client_fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
  opt = 4096;
  ::setsockopt(m_client_fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
  struct sockaddr_in sa;
  sa.sin_family = AF_INET;
  sa.sin_port = htons(9002);
  inet_aton("127.0.0.1", &sa.sin_addr);
  int res = ::connect(m_client_fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
  if (res == -1 && errno != EINPROGRESS)
    perror("connect");
  struct sockaddr_in accept_addr;
  socklen_t addrlen = sizeof(accept_addr);
  m_accept_fd = accept4(listen_fd, reinterpret_cast<sockaddr*>(&accept_addr), &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
  cout << "Accepted socket: " << m_accept_fd << endl;
}

//static
char Socket::s_buffer[1024 * 1024];

void Socket::send(int n)
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> Socket::send(" << n << ")" << endl;
  }
  assert(n < (int)sizeof(s_buffer));
  for (;;)
  {
    ssize_t ret;
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      cout << "\e[32mwrite(" << m_accept_fd << ", buffer, " << n << ") = ";
      ret = ::write(m_accept_fd, s_buffer, n);
      cout << ret << "\e[0m";
      if (ret == -1)
        cout << " (" << std::strerror(errno) << ')';
      cout << endl;
    }
    if (ret == -1)
    {
      if (errno != EAGAIN)
      {
        perror("write");
        assert(ret >= 0);
      }
      break;
    }
    m_sent += ret;
    break;
  }
}

void Socket::close_send()
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> Socket::close_send()" << endl;
  }
  ssize_t ret;
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << "\e[32mclose(" << m_accept_fd << ") = ";
    ret = ::close(m_accept_fd);
    cout << ret << "\e[0m";
    if (ret == -1)
      cout << " (" << std::strerror(errno) << ')';
    cout << endl;
  }
  assert(ret >= 0);
}

void Socket::recv(int n)
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> Socket::recv(" << n << ")" << endl;
  }
  assert(n < (int)sizeof(s_buffer));
  ssize_t ret = -1;
  while (ret == -1)
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << "\e[32mread(" << m_accept_fd << ", buffer, " << n << ") = ";
    ret = ::read(m_accept_fd, s_buffer, n);
    cout << ret << "\e[0m" << endl;
  }
  if (ret == 0)
    cout << "  [EOF]" << endl;
  else
    m_received += ret;
}

void Socket::write(int n)
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> Socket::write(" << n << ")" << endl;
  }
  assert(n < (int)sizeof(s_buffer));
  for (;;)
  {
    ssize_t ret;
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      cout << "\e[32mwrite(" << m_client_fd << ", buffer, " << n << ") = ";
      ret = ::write(m_client_fd, s_buffer, n);
      cout << ret << "\e[0m";
      if (ret == -1)
        cout << " (" << std::strerror(errno) << ')';
      cout << endl;
    }
    if (ret == -1)
    {
      if (errno != EAGAIN)
      {
        perror("write");
        assert(ret >= 0);
      }
      break;
    }
    m_written += ret;
    break;
  }
}

void Socket::read(int n)
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> Socket::read(" << n << ")" << endl;
  }
  assert(n < (int)sizeof(s_buffer));
  ssize_t ret = -1;
  while (ret == -1)
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << "\e[32mread(" << m_client_fd << ", buffer, " << n << ") = ";
    ret = ::read(m_client_fd, s_buffer, n);
    cout << ret << "\e[0m" << endl;
  }
  if (ret == 0)
    cout << "  [EOF]" << endl;
  else
    m_read += ret;
}

struct EpollThread : thread_permuter::Thread
{
  Epoll& m_ep;
  bool m_enter_epoll_wait;
  bool m_wait_for_returned_from_epoll_wait;

  EpollThread(Epoll& ep);
  ~EpollThread() { stop(); }

  void event_loop();

  void enter_epoll_wait();
  void exit_epoll_wait();
  void wait_for_returned_from_epoll_wait();
};

EpollThread::EpollThread(Epoll& ep) : thread_permuter::Thread([this](){ event_loop(); }),
  m_ep(ep), m_enter_epoll_wait(false), m_wait_for_returned_from_epoll_wait(false)
{
  start('*');
}

void EpollThread::event_loop()
{
  while (!m_last_permutation)
  {
    struct epoll_event events;
    int timeout = 1;
    int ready;
    do
    {
      if (timeout == -1)
      {
        cout << get_name() << " \e[32mNo events!\e[0m" << endl;
        pause(thread_permuter::blocking, true);
      }
      else
        cout << get_name() << " \e[32mepoll_wait(" << m_ep.m_epoll_fd << ", &events, 1, -1) =\e[0m <unfinished>..." << endl;
      ready = epoll_wait(m_ep.m_epoll_fd, &events, 1, timeout);
      if (ready != 0)
      {
        // Block the main thread somewhere until we're done printing the result.
        std::lock_guard<std::mutex> lock(cout_mutex);
        cout << get_name() << " ...<continued> ";
        if (ready == -1)
          cout << "\e[32m-1\e[0m (" << std::strerror(errno) << ')' << "\e[0m" << endl;
        else
          cout << "\e[32m" << ready << "\e[0m; events: " << events_to_str(events.events) << "\e[0m" << endl;
      }
      else
        timeout = -1;
    }
    while (ready == 0);
    if (ready == -1)
      continue;
    if (!m_last_permutation)
    {
      pause(thread_permuter::yielding);
      if (m_wait_for_returned_from_epoll_wait)
      {
        //cout << get_name() << " Entering second pause() after returning from epoll_wait." << endl;
        pause(thread_permuter::yielding);
      }
    }
  }
}

void EpollThread::enter_epoll_wait()
{
  cout << ">> EpollThread::enter_epoll_wait()" << endl;
  m_enter_epoll_wait = true;
  step();
}

void EpollThread::wait_for_returned_from_epoll_wait()
{
  {
    std::lock_guard<std::mutex> lock(cout_mutex);
    cout << ">> EpollThread::wait_for_returned_from_epoll_wait()" << endl;
  }
  m_wait_for_returned_from_epoll_wait = true;
  step();
}

void EpollThread::exit_epoll_wait()
{
  cout << ">> EpollThread::exit_epoll_wait()" << endl;
  m_enter_epoll_wait = false;
  wakeup();
  step();
}

int main()
{
  {
    PipeReadEnd pipe_read_end;
    Epoll ep(pipe_read_end.m_pipefd[0]);
    EpollThread epoll_thread(ep);

    ep.add_fd_with_events(EPOLLIN);             // fd is readable, but no data --> no events.

    pipe_read_end.send(10000);                  // Writes 8196 bytes. fd becomes readable --> EPOLLIN
    pipe_read_end.close_send();
    epoll_thread.enter_epoll_wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    epoll_thread.enter_epoll_wait();            // EPOLLHUP?
    epoll_thread.enter_epoll_wait();            // EPOLLHUP?

    pipe_read_end.read(2048);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    epoll_thread.enter_epoll_wait();            // EPOLLHUP?
    epoll_thread.enter_epoll_wait();            // EPOLLHUP?
  }
  cout << "Leaving main()." << endl;
}
