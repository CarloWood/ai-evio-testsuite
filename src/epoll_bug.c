// Compile as:
//
//   gcc -g epoll_bug.c -o epoll_bug
//
// Run
//
//   ./epoll_bug
//
// and observe that epoll_wait() starts stalling.
// On my box the stalls are between 205 and 208 ms (mostly 207)
// and appear to be a multiple of 41 ms (I have seen values of 41, 123 and 165 ms too).
//
// This test program establishes a AF_INET socket---socket connection and
// writes data to one socket until 1GB has been written. At the same time
// data is read on the other end AND written back, and finally read again
// on the initial socket.
//
// All reads and writes are done after epoll_wait() (level triggered) says
// that it is possible to read / write respectively. Under normal circumstances
// that is non-stop and calls to epoll_wait only take microseconds.
//
// However, after a while the system gets into a state where - although
// a LOT more was written back than was read on the initial socket yet -
// epoll_wait() is no longer returning instantly but gets stalled for ~207 ms
// every call, slowing down the transfer with a factor of 1000 or more.
//
// When the sizes of the sndbuf and rcvbuf are changed, the problem can
// go away; for example set both to 65536 bytes and the program finishes in
// roughtly 2 seconds when using a burst_size of 1000000000.
//
//   >time ./epoll_bug
//   Total written from fd 6 ==> 5: 1000000000; total read: 1000000000; still in the pipe line: 0 bytes.
//   Total written from fd 5 ==> 6: 1000000000; total read: 1000000000; still in the pipe line: 0 bytes.
//
//   real    0m2,052s
//   user    0m0,156s
//   sys     0m1,868s
//

// Define this to 1 to print something at every read or write event.
#define VERBOSE 0

// The amount of bytes to sent over the TCP/IP connection (in both ways).
int burst_size = 10000000;
int read_length = 100000;

// The socket send and receive buffer sizes of both sockets.
#define SIZE 4096
int const sndbuf_size = 33181;
int const rcvbuf_size = SIZE;



#define _GNU_SOURCE     // For accept4.
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/time.h>

struct Epoll;
struct EventObj;
struct ListenSocket;
struct ClientSocket;
struct AcceptSocket;

typedef struct Epoll Epoll;
typedef struct EventObj EventObj;
typedef struct ListenSocket ListenSocket;
typedef struct ClientSocket ClientSocket;
typedef struct AcceptSocket AcceptSocket;

bool ListenSocket_read_event(EventObj* self_event, Epoll* epoll_obj);
bool AcceptSocket_write_event(EventObj* self_event, Epoll* epoll_obj);
bool ClientSocket_read_event(EventObj* self_event, Epoll* epoll_obj);
bool ClientSocket_write_event(EventObj* event_obj, Epoll* epoll_obj);
bool AcceptSocket_read_event(EventObj* self_event, Epoll* epoll_obj);

struct Epoll
{
  int fd;
  char* buf;
  int watched_count;
  size_t twlen1;
  size_t twlen2;
  size_t trlen1;
  size_t trlen2;
};

void Epoll_init(Epoll* self)
{
  self->fd = epoll_create1(EPOLL_CLOEXEC);
  self->buf = calloc(1, 10000000);
  self->watched_count = 0;
  self->twlen1 = 0;
  self->twlen2 = 0;
  self->trlen1 = 0;
  self->trlen2 = 0;
}

struct EventObj
{
  int fd;
  int watched_events;
  bool (*read_event)(EventObj*, Epoll*);
  bool (*write_event)(EventObj*, Epoll*);
};

void Epoll_add(Epoll* self, EventObj* event_obj, int events)
{
  // Don't add events that are already watched.
  assert(!(event_obj->watched_events & events));
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = event_obj->watched_events | events;
  event.data.ptr = event_obj;
  epoll_ctl(self->fd, (event.events != events) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD, event_obj->fd, &event);
  int count = 0;
  for (int ev = EPOLLIN; ; ev = EPOLLOUT)
  {
    if ((events & ev))
      ++count;
    if (ev == EPOLLOUT)
      break;
  }
  event_obj->watched_events = event.events;
  self->watched_count += count;
}

void Epoll_add_listen_socket(Epoll* self, EventObj* event_obj)
{
  event_obj->read_event = &ListenSocket_read_event;
  event_obj->write_event = NULL;
  Epoll_add(self, event_obj, EPOLLIN);
}

void Epoll_add_client_socket(Epoll* self, EventObj* event_obj)
{
  event_obj->read_event = &ClientSocket_read_event;
  event_obj->write_event = &ClientSocket_write_event;
  Epoll_add(self, event_obj, EPOLLIN);
}

void Epoll_add_accept_socket(Epoll* self, EventObj* event_obj)
{
  event_obj->read_event = &AcceptSocket_read_event;
  event_obj->write_event = &AcceptSocket_write_event;
  Epoll_add(self, event_obj, EPOLLIN|EPOLLOUT);
}

bool Epoll_remove(Epoll* self, EventObj* event_obj, int events)
{
  // Don't remove events that aren't watched.
  assert((event_obj->watched_events & events) == events);
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = event_obj->watched_events & ~events;
  event.data.ptr = event_obj;
  epoll_ctl(self->fd, event.events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL, event_obj->fd, &event);
  int count = 0;
  for (int ev = EPOLLIN; ; ev = EPOLLOUT)
  {
    if ((events & ev))
      ++count;
    if (ev == EPOLLOUT)
      break;
  }
  event_obj->watched_events = event.events;
  if (!event_obj->watched_events)
    close(event_obj->fd);
  self->watched_count -= count;
  return self->watched_count;
}

void Epoll_mainloop(Epoll* self)
{
  struct epoll_event events[4];
  memset(events, 0, sizeof(events));
  for (;;)
  {
    struct timeval tv1;
    gettimeofday(&tv1, NULL);
    int n = epoll_wait(self->fd, events, 4, 1100);
    struct timeval tv2;
    gettimeofday(&tv2, NULL);
    tv2.tv_sec -= tv1.tv_sec;
    if ((tv2.tv_usec -= tv1.tv_usec) < 0)
    {
      tv2.tv_usec += 1000000;
      --tv2.tv_sec;
    }
    if (tv2.tv_usec > 10000)
    {
      if (tv2.tv_sec == 0)
        printf("epoll_wait() stalled %ld milliseconds!\n", tv2.tv_usec / 1000);
      else
      {
        printf("epoll_wait() blocked permanently!\n");
        return;
      }
    }
    while (n--)
    {
      if ((events[n].events & EPOLLIN))
      {
        EventObj* event_obj = (EventObj*)events[n].data.ptr;
        if (!event_obj->read_event(event_obj, self))
        {
          if (!Epoll_remove(self, event_obj, EPOLLIN))
            return;
        }
      }
      if ((events[n].events & EPOLLOUT))
      {
        EventObj* event_obj = (EventObj*)events[n].data.ptr;
        if (!event_obj->write_event(event_obj, self))
        {
          if (!Epoll_remove(self, event_obj, EPOLLOUT))
            return;
        }
      }
      assert(!(events[n].events & ~(EPOLLIN|EPOLLOUT)));
    }
  }
}

struct AcceptSocket
{
  EventObj event;
};

bool AcceptSocket_write_event(EventObj* self_event, Epoll* epoll_obj)
{
  int const as = 11;
  int alen[11] = { 2016, 2016, 4064, 8160, 16384, 32832, 65728, 131520, 263104, 526272, 1052608 };
  int i = 0;
  int len, wlen;
  int sum = 0;
  int max = burst_size - epoll_obj->twlen1;
  do
  {
    len = alen[i++];
    if (len > max)
      len = max;
    wlen = write(self_event->fd, epoll_obj->buf, len);
    if (wlen > 0)
    {
      epoll_obj->twlen1 += wlen;
      max -= wlen;
      sum += wlen;
    }
  }
  while (wlen == len && i < as && max > 0);

#if VERBOSE
  printf("wrote %d bytes to %d (total written now %lu), now in pipe line: %ld\n", sum, self_event->fd, epoll_obj->twlen1, epoll_obj->twlen1 - epoll_obj->trlen1);
#endif

  return  epoll_obj->twlen1 < burst_size;
}

bool AcceptSocket_read_event(EventObj* self_event, Epoll* epoll_obj)
{
  int len = read_length;
  int rlen;
  int sum = 0;
  do
  {
    rlen = read(self_event->fd, epoll_obj->buf, len);
    if (rlen > 0)
    {
      epoll_obj->trlen2 += rlen;
      sum += rlen;
    }
  }
  while (rlen > 0);
#if VERBOSE
  printf("Read %d bytes from fd %d (total read now %lu), left in pipe line: %ld\n", sum, self_event->fd, epoll_obj->trlen2, epoll_obj->twlen2 - epoll_obj->trlen2);
#endif
  return rlen != 0;
}

void AcceptSocket_init(AcceptSocket* self, int listen_fd, Epoll* epoll_obj)
{
  memset(&self->event, 0, sizeof(EventObj));
  self->event.fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

  Epoll_add_accept_socket(epoll_obj, &self->event);
}

struct ListenSocket
{
  EventObj event;
  struct sockaddr_in address;
  AcceptSocket* accept_socket;
};

bool ListenSocket_read_event(EventObj* self_event, Epoll* epoll_obj)
{
  ListenSocket* self = (ListenSocket*)self_event;
  self->accept_socket = (AcceptSocket*)calloc(sizeof(AcceptSocket), 1);
  AcceptSocket_init(self->accept_socket, self_event->fd, epoll_obj);
  return 0;
}

void ListenSocket_init(ListenSocket* self, Epoll* epoll_obj)
{
  memset(&self->event, 0, sizeof(EventObj));
  self->event.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  int opt = 1;
  setsockopt(self->event.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#if 0
  // These socket buffer sizes are inherited by the accepted sockets!
  opt = rcvbuf_size;
  setsockopt(self->event.fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
  opt = sndbuf_size;
  setsockopt(self->event.fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
#endif
  memset(&self->address, 0, sizeof(struct sockaddr_in));
  self->address.sin_family = AF_INET;
  self->address.sin_port = htons(9001);
  bind(self->event.fd, (struct sockaddr*)&self->address, sizeof(struct sockaddr_in));
  listen(self->event.fd, 4);

  Epoll_add_listen_socket(epoll_obj, &self->event);
}

struct ClientSocket
{
  EventObj event;
};

bool ClientSocket_read_event(EventObj* self_event, Epoll* epoll_obj)
{
  bool buffer_empty = epoll_obj->trlen1 - epoll_obj->twlen2 == 0;
  int len = read_length;
  int rlen;
  int sum = 0;
  do
  {
    rlen = read(self_event->fd, epoll_obj->buf, len);
    if (rlen > 0)
    {
      epoll_obj->trlen1 += rlen;
      sum += rlen;
    }
  }
  while (rlen > 0);
#if VERBOSE
  printf("Read %d bytes from fd %d (total read now %lu), left in pipe line: %ld\n", sum, self_event->fd, epoll_obj->trlen1, epoll_obj->twlen1 - epoll_obj->trlen1);
#endif
  if (buffer_empty && epoll_obj->trlen1 > epoll_obj->twlen2)
    Epoll_add(epoll_obj, self_event, EPOLLOUT);
  return 1;
}

bool ClientSocket_write_event(EventObj* event_obj, Epoll* epoll_obj)
{
  // We should only get here when there is something to write.
  assert(epoll_obj->trlen1 - epoll_obj->twlen2 > 0);
  int wlen = write(event_obj->fd, epoll_obj->buf, epoll_obj->trlen1 - epoll_obj->twlen2);
#if VERBOSE
  printf("write(%d, buf, %ld) = %d (total written now %lu), now in pipe line: %ld\n",
      event_obj->fd, epoll_obj->trlen1 - epoll_obj->twlen2, wlen, wlen + epoll_obj->twlen2, wlen + epoll_obj->twlen2 - epoll_obj->trlen2);
#endif
  if (wlen <= 0)
    return 0;
  epoll_obj->twlen2 += wlen;
  if (epoll_obj->twlen2 >= burst_size && epoll_obj->twlen2 == epoll_obj->twlen1)
    Epoll_remove(epoll_obj, event_obj, EPOLLIN);
  return (epoll_obj->trlen1 - epoll_obj->twlen2) > 0 ? 1 : 0;
}

void ClientSocket_init(ClientSocket* self, struct sockaddr_in* address, Epoll* epoll_obj)
{
  memset(&self->event, 0, sizeof(EventObj));
  self->event.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  int opt = rcvbuf_size;
  setsockopt(self->event.fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
  opt = sndbuf_size;
  setsockopt(self->event.fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
  connect(self->event.fd, (struct sockaddr*)address, sizeof(struct sockaddr_in));

  Epoll_add_client_socket(epoll_obj, &self->event);
}

int main()
{
  Epoll epoll_obj;
  Epoll_init(&epoll_obj);

  ListenSocket listen_obj;
  ListenSocket_init(&listen_obj, &epoll_obj);

  ClientSocket client_obj;
  ClientSocket_init(&client_obj, &listen_obj.address, &epoll_obj);

  Epoll_mainloop(&epoll_obj);

  AcceptSocket* accept_ptr = listen_obj.accept_socket;

  printf("Total written from fd %d ==> %d: %lu; total read: %lu; still in the pipe line: %ld bytes.\n",
      accept_ptr->event.fd, client_obj.event.fd, epoll_obj.twlen1, epoll_obj.trlen1, epoll_obj.twlen1 - epoll_obj.trlen1);
  printf("Total written from fd %d ==> %d: %lu; total read: %lu; still in the pipe line: %ld bytes.\n",
      client_obj.event.fd, accept_ptr->event.fd, epoll_obj.twlen2, epoll_obj.trlen2, epoll_obj.twlen2 - epoll_obj.trlen2);
}
