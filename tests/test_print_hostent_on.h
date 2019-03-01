#include "evio/inet_support.h"
#include <sstream>
#include <netdb.h>
extern int h_errno;

using ::testing::MatchesRegex;

TEST(inet_support, print_hostent_on) {
  // Preperation.
  char const* host_name = "www.yahoo.com";
  // Suppress debug output
  // WARNING : Cannot find symbol _end
  // WARNING : Unknown size of symbol _nss_mdns4_minimal_gethostbyaddr_r
  Debug(dc::warning.off());
  struct hostent* hp = gethostbyname(host_name);
  ASSERT_TRUE(hp) << "gethostbyname: " << hstrerror(h_errno);
  Debug(dc::warning.on());

  // Call function under test.
  std::ostringstream ss;
  ASSERT_FALSE(evio::print_hostent_on(hp, ss));

  // Print the result.
  Dout(dc::notice, "evio::print_hostent_on(\"" << host_name << "\") -->\n" << ss.str());

  // Examine the result.
  EXPECT_THAT(ss.str(), MatchesRegex(
        ".*[^w]w{0,2}\\.yahoo\\.com\".*\n"             // Official name (ends on .yahoo.com but isn't www.yahoo.com).
        ".*[Aa]lias.*\"www\\.yahoo\\.com\".*\n"        // At least one alias "www.yahoo.com".
        ".*[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+.*\n"      // At least two IP#'s.
        ".*[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+.*\n"
        ".*"));
}
