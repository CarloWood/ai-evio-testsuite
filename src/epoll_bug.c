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
typedef int bool;

bool ListenSocket_read_event(EventObj* self_event, Epoll* epoll_obj);
bool AcceptSocket_write_event(EventObj* self_event, Epoll* epoll_obj);
bool ClientSocket_read_event(EventObj* self_event, Epoll* epoll_obj);

struct Epoll
{
  int fd;
  char* buf;
  int watched_count;
  size_t twlen;
  size_t trlen;
};

void Epoll_init(Epoll* self)
{
  self->fd = epoll_create1(EPOLL_CLOEXEC);
  self->buf = calloc(1, 1000000);
  self->watched_count = 0;
  self->twlen = 0;
  self->trlen = 0;
}

struct EventObj
{
  int fd;
  int watched_events;
  bool (*read_event)(EventObj*, Epoll*);
  bool (*write_event)(EventObj*, Epoll*);
};

void Epoll_add_listen_socket(Epoll* self, EventObj* event_obj)
{
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = EPOLLIN;
  event.data.ptr = event_obj;
  event_obj->watched_events = event.events;
  event_obj->read_event = &ListenSocket_read_event;
  event_obj->write_event = NULL;
  epoll_ctl(self->fd, EPOLL_CTL_ADD, event_obj->fd, &event);
  self->watched_count++;
}

void Epoll_add_client_socket(Epoll* self, EventObj* event_obj)
{
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = EPOLLIN /*|EPOLLOUT*/;
  event.data.ptr = event_obj;
  event_obj->watched_events = event.events;
  event_obj->read_event = &ClientSocket_read_event;
  event_obj->write_event = NULL;
  epoll_ctl(self->fd, EPOLL_CTL_ADD, event_obj->fd, &event);
  self->watched_count++;
}

void Epoll_add_accept_socket(Epoll* self, EventObj* event_obj)
{
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = /*EPOLLIN|*/EPOLLOUT;
  event.data.ptr = event_obj;
  event_obj->watched_events = event.events;
  event_obj->read_event = NULL;
  event_obj->write_event = &AcceptSocket_write_event;
  epoll_ctl(self->fd, EPOLL_CTL_ADD, event_obj->fd, &event);
  self->watched_count++;
}

bool Epoll_remove(Epoll* self, EventObj* event_obj, int events)
{
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
    int n = epoll_wait(self->fd, events, 4, -1);
    while (n--)
    {
      if ((events[n].events & EPOLLIN))
      {
        EventObj* event_obj = (EventObj*)events[n].data.ptr;
        if (!event_obj->read_event(event_obj, self))
        {
          if (!Epoll_remove(self, event_obj, EPOLLIN))
            exit(0);
        }
      }
      if ((events[n].events & EPOLLOUT))
      {
        EventObj* event_obj = (EventObj*)events[n].data.ptr;
        if (!event_obj->write_event(event_obj, self))
        {
          if (!Epoll_remove(self, event_obj, EPOLLOUT))
            exit(0);
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
  int const as = 5;
  int alen[5] = { 2016, 2016, 4064, 8160, 16384 };
  int i = 0;
  int len, wlen;
  do
  {
    len = alen[i++];
    wlen = write(self_event->fd, epoll_obj->buf, len);
    if (wlen > 0)
      epoll_obj->twlen += wlen;
  }
  while (wlen == len && i < as);

#if 0
  // At this point remove the EPOLLOUT event request again from connect_sock_fd.
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.events = EPOLLIN;
  event.data.ptr = self_event;
  epoll_ctl(epoll_obj->fd, EPOLL_CTL_MOD, self_event->fd, &event);
#endif

  return  epoll_obj->twlen < 100000000;
//    epoll_ctl(epoll_obj->fd, EPOLL_CTL_DEL, self_event->fd, NULL);
//    close(self_event->fd);
}

void AcceptSocket_init(AcceptSocket* self, int listen_fd, Epoll* epoll_obj)
{
  memset(&self->event, 0, sizeof(EventObj));
  self->event.fd = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
  int opt = 4096;
  setsockopt(self->event.fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
  opt = 4096;
  setsockopt(self->event.fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));

  Epoll_add_accept_socket(epoll_obj, &self->event);
}

struct ListenSocket
{
  EventObj event;
  struct sockaddr_in address;
};

bool ListenSocket_read_event(EventObj* self_event, Epoll* epoll_obj)
{
  AcceptSocket* accept_socket = (AcceptSocket*)calloc(sizeof(AcceptSocket), 1);
  AcceptSocket_init(accept_socket, self_event->fd, epoll_obj);
  return 0;
}

void ListenSocket_init(ListenSocket* self, Epoll* epoll_obj)
{
  memset(&self->event, 0, sizeof(EventObj));
  self->event.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  int opt = 1;
  setsockopt(self->event.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
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
  int len = 992;
  int rlen;
  do
  {
    rlen = read(self_event->fd, epoll_obj->buf, len);
    if (rlen > 0)
      epoll_obj->trlen += rlen;
  }
  while (rlen > 0);
  if (rlen == 0)
  {
    printf("Total written: %lu; total read: %lu\n", epoll_obj->twlen, epoll_obj->trlen);
    return 0;
  }
  return 1;
}

void ClientSocket_init(ClientSocket* self, struct sockaddr_in* address, Epoll* epoll_obj)
{
  memset(&self->event, 0, sizeof(EventObj));
  self->event.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  int opt = 4096;
  setsockopt(self->event.fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
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
}
