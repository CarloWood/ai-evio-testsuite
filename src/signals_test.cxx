#include "sys.h"
#include "debug.h"
#include "utils/AISignals.h"
#include "signal_safe_printf.h"
#include "utils/cpu_relax.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <sys/epoll.h>

using utils::Signals;

extern "C" void sigint_cb(int signum)
{
  signal_safe_printf("Calling sigint_cb(%d)\n", signum);
}

std::atomic_bool done = false;

extern "C" void my_cb(int signum)
{
  signal_safe_printf("<<<Calling my_cb(%d)>>>", signum);
  done = true;
}

void thread1(int sn1)
{
  Debug(NAMESPACE_DEBUG::init_thread());

  Signals::unblock(sn1, my_cb);

  while (!done)
    cpu_relax();

  Dout(dc::notice, "Leaving thread1.");
}

void thread2(int sn2, int sn3)
{
  Debug(NAMESPACE_DEBUG::init_thread());

  sigset_t pwait_sigmask;
  sigemptyset(&pwait_sigmask);
  sigprocmask(0, NULL, &pwait_sigmask);
  sigdelset(&pwait_sigmask, sn2);
  sigdelset(&pwait_sigmask, sn3);

  int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  constexpr int maxevents = 32;
  struct epoll_event s_events[maxevents];
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  Dout(dc::notice|flush_cf|continued_cf, "Entering epoll_pwait() = ");
  CWDEBUG_ONLY(int ready =) epoll_pwait(epoll_fd, s_events, maxevents, -1, &pwait_sigmask);
  Dout(dc::finish|cond_error_cf(ready == -1), ready);

  Dout(dc::notice, "Leaving thread2.");
}

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  AISignals s1({});
  AISignals s2({}, 1);
  AISignals s3({SIGINT});
  AISignals s4({SIGINT}, 1);
  AISignals s5({SIGINT, SIGABRT});
  AISignals signals({SIGINT, SIGABRT}, 3);

  int sn1 = Signals::next_rt_signum();
  int sn2 = Signals::next_rt_signum();
  int sn3 = Signals::next_rt_signum();
  signals.register_callback(sn2, my_cb);
  signals.register_callback(sn3, my_cb);

  std::thread t1(thread1, sn1);
  std::thread t2(thread2, sn2, sn3);

  // Accept ^C.
  Signals::default_handler(SIGINT);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  Dout(dc::notice, "Sending signals...");
  pthread_kill(t1.native_handle(), sn1);
  pthread_kill(t2.native_handle(), sn2);
  pthread_kill(t2.native_handle(), sn3);

  t1.join();
  t2.join();
  Dout(dc::notice, signals);
}
