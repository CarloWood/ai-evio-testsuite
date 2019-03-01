#include "evio/inet_support.h"
#include <netinet/in.h>
#include <sys/un.h>

TEST(inet_support, size_of_addr) {
  // Preparation.
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
  struct sockaddr_un sun;
  sin.sin_family = AF_INET;
  sin6.sin6_family = AF_INET6;
  sun.sun_family = AF_UNIX;
  struct sockaddr const* sin_ptr = reinterpret_cast<struct sockaddr const*>(&sin);
  struct sockaddr const* sin6_ptr = reinterpret_cast<struct sockaddr const*>(&sin6);
  struct sockaddr const* sun_ptr = reinterpret_cast<struct sockaddr const*>(&sun);

  // Call function under test.
  EXPECT_EQ(evio::size_of_addr(sin_ptr), sizeof(struct sockaddr_in));
  EXPECT_EQ(evio::size_of_addr(sin6_ptr), sizeof(struct sockaddr_in6));
  EXPECT_EQ(evio::size_of_addr(sun_ptr), sizeof(struct sockaddr_un));
}
