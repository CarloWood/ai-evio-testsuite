#include "sys.h"
#include "evio/protocol/xmlrpc/initialize.h"
#include "utils/AIAlert.h"
#include "utils/debug_ostream_operators.h"
#include "debug.h"

int main()
{
  Debug(debug::init());

  std::string base64_str = "SGVsbG8gd29ybGQ=";
  std::string iso8601_str = "2021-01-29T15:37:23Z";

  try
  {
    evio::BinaryData b1;
    evio::protocol::xmlrpc::initialize(b1, base64_str);
    Dout(dc::notice, "b1 = " << b1);

    evio::DateTime d1;
    evio::protocol::xmlrpc::initialize(d1, iso8601_str);
    Dout(dc::notice, "d1 = " << d1);
  }
  catch (AIAlert::Error const& error)
  {
    Dout(dc::warning, error);
  }
}
