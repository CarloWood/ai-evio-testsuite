#include "sys.h"
#include "debug.h"
#include <netinet/in.h>

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  Dout(dc::notice, "sizeof(struct sockaddr_in) = " << sizeof(struct sockaddr_in));
  Dout(dc::notice, "sizeof(struct sockaddr_in6) = " << sizeof(struct sockaddr_in6));
  Dout(dc::notice, "sizeof(struct sockaddr_storage) = " << sizeof(struct sockaddr_storage));
}
