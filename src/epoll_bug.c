#define _GNU_SOURCE     // For accept4.
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

int epoll_fd;
int listen_fd;
int connect_sock_fd;
int accept_sock_fd;

int main()
{
  epoll_fd = epoll_create1(EPOLL_CLOEXEC);

  listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  struct sockaddr_in address;
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(9001);
  bind(listen_fd, (struct sockaddr*)&address, sizeof(address));
  listen(listen_fd, 4);
  struct epoll_event events[4];
  memset(events, 0, sizeof(events));
  events[0].events = EPOLLIN;
  events[0].data.fd = listen_fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &events[0]);

  connect_sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

  // If we connect BEFORE...
  connect(connect_sock_fd, (struct sockaddr*)&address, sizeof(address));

  // ...setting the SO_RCVBUF, then things go wrong.
  opt = 4096;
  setsockopt(connect_sock_fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));

  // If the connect() is moved to here, then everything works as expected.
  //connect(connect_sock_fd, (struct sockaddr*)&address, sizeof(address));

  events[0].events = EPOLLIN;
  events[0].data.fd = connect_sock_fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connect_sock_fd, &events[0]);
  events[0].events = EPOLLIN|EPOLLOUT;
  events[0].data.fd = connect_sock_fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_MOD, connect_sock_fd, &events[0]);

  char* buf = calloc(1, 1000000);

  size_t twlen = 0;
  size_t trlen = 0;
  for (;;)
  {
    int n = epoll_wait(epoll_fd, events, 4, -1);
    while (n--)
    {
      if (events[n].data.fd == listen_fd)
      {
        socklen_t addr_len = sizeof(address);
        accept_sock_fd = accept4(listen_fd, (struct sockaddr*)&address, &addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        opt = 4096;
        setsockopt(accept_sock_fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
        opt = 4096;
        setsockopt(accept_sock_fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
        events[n].events = EPOLLIN;
        events[n].data.fd = accept_sock_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, accept_sock_fd, &events[n]);
        events[n].events = EPOLLIN|EPOLLOUT;
        events[n].data.fd = accept_sock_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, accept_sock_fd, &events[n]);
        close(listen_fd);
      }
      else if (events[n].data.fd == accept_sock_fd)
      {
        int const as = 5;
        int alen[5] = { 2016, 2016, 4064, 8160, 16384 };
        int i = 0;
        int len, wlen;
        do
        {
          len = alen[i++];
          wlen = write(accept_sock_fd, buf, len);
          if (wlen > 0)
            twlen += wlen;
        }
        while (wlen == len && i < as);

        // At this point remove the EPOLLOUT event request again from connect_sock_fd.
        events[n].events = EPOLLIN;
        events[n].data.fd = connect_sock_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, connect_sock_fd, &events[n]);

        if (twlen > 100000000)
        {
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, accept_sock_fd, NULL);
          close(accept_sock_fd);
        }
      }
      else if (events[n].data.fd == connect_sock_fd && (events[n].events & EPOLLIN))
      {
        int len = 992;
        int rlen;
        do
        {
          rlen = read(connect_sock_fd, buf, len);
          if (rlen > 0)
            trlen += rlen;
        }
        while (rlen > 0);
        if (rlen == 0)
        {
          printf("Total written: %lu; total read: %lu\n", twlen, trlen);
          return 0;
        }
      }
    }
  }

  free(buf);
}
