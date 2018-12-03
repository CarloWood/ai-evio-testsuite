#include "sys.h"
#include "debug.h"
#include "evio/SocketAddress.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"

int main(int argc, char* argv[])
{
  Debug(NAMESPACE_DEBUG::init());

  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << ' ' << "<IP-number>" << std::endl;
    return 1;
  }

  try
  {
    evio::SocketAddress sa(argv[1], 0);

    evio::SocketAddress::arpa_buf_t arpa_buf;
    sa.ptr_qname(arpa_buf);

    Dout(dc::notice, "sa = " << sa << "; ptr qname = \"" << arpa_buf.data() << "\".");
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}
