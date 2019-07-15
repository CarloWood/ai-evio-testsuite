#include "sys.h"
#include "debug.h"
#include "evio/Interface.h"
#include "evio/SocketNetmask.h"

int main()
{
  Debug(NAMESPACE_DEBUG::init());

  evio::Interfaces interfaces;

  Debug(dc::warning.off());
  for (auto interface : interfaces)
    std::cout << interface << '\n';
  Debug(dc::warning.on());
}
