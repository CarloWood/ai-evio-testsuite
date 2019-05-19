#include "evio/SocketAddress.h"
#include "utils/AIAlert.h"
#include <mutex>

using testing::ContainsRegex;
using testing::MatchesRegex;
using SocketAddress = evio::SocketAddress;
using SocketAddressDeathTest = SocketAddress;

static void test_is_unspecified(evio::SocketAddress const& sa)
{
  EXPECT_TRUE(sa.is_unspecified());
  EXPECT_FALSE(sa.is_un() || sa.is_ip() || sa.is_ip4() || sa.is_ip6());
  EXPECT_THAT(sa.to_string(), ContainsRegex("[Uu][Nn][Ss][Pp][Ee][Cc]"));
  EXPECT_EQ(AF_UNSPEC, sa.family());

  evio::SocketAddress::arpa_buf_t buf;
  EXPECT_DEATH({ sa.ptr_qname(buf); }, "SocketAddress::ptr_qname called.*[Uu][Nn][Ss][Pp][Ee][Cc]");
}

static void test_is_ip4(evio::SocketAddress const& sa)
{
  EXPECT_TRUE(sa.is_ip4());
  EXPECT_TRUE(sa.is_ip());
  EXPECT_FALSE(sa.is_unspecified() || sa.is_un() || sa.is_ip6());
  EXPECT_THAT(sa.to_string(), MatchesRegex("[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}:[0-9]{1,5}"));
  EXPECT_EQ(AF_INET, sa.family());

  evio::SocketAddress::arpa_buf_t buf;
  sa.ptr_qname(buf);
  char const* b = &buf[0];
  size_t len = strlen(b);
  ASSERT_LT(len, buf.size());
  std::string arpa_str(b, len);
  EXPECT_THAT(arpa_str, MatchesRegex("[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.in-addr\\.arpa\\."));
}

static void test_is_ip6(evio::SocketAddress const& sa)
{
  EXPECT_TRUE(sa.is_ip6());
  EXPECT_TRUE(sa.is_ip());
  EXPECT_FALSE(sa.is_unspecified() || sa.is_un() || sa.is_ip4());
  EXPECT_THAT(sa.to_string(), MatchesRegex("\\[[0-9a-f:]*\\]:[0-9]{1,5}"));
  EXPECT_EQ(AF_INET6, sa.family());

  evio::SocketAddress::arpa_buf_t buf;
  sa.ptr_qname(buf);
  char const* b = &buf[0];
  size_t len = strlen(b);
  ASSERT_LT(len, buf.size());
  std::string arpa_str(b, len);
  EXPECT_THAT(arpa_str, MatchesRegex("([0-9a-f]\\.){32}ip6\\.arpa\\."));
}

static void test_is_un(evio::SocketAddress const& sa)
{
  EXPECT_TRUE(sa.is_un());
  EXPECT_FALSE(sa.is_unspecified() || sa.is_ip4() || sa.is_ip6() || sa.is_ip());
  EXPECT_THAT(sa.to_string(), MatchesRegex("/.*"));
  EXPECT_EQ(AF_UNIX, sa.family());

  evio::SocketAddress::arpa_buf_t buf;
  EXPECT_DEATH({ sa.ptr_qname(buf); }, "SocketAddress::ptr_qname called for /.*isn't an IP");
}

static void test_equal(evio::SocketAddress const& sa1, evio::SocketAddress const& sa2)
{
  EXPECT_EQ(sa1, sa2);
  EXPECT_TRUE(sa1.is_ip4() == sa2.is_ip4());
  EXPECT_TRUE(sa1.is_ip6() == sa2.is_ip6());
  EXPECT_TRUE(sa1.is_ip() == sa2.is_ip());
  EXPECT_TRUE(sa1.is_unspecified() == sa2.is_unspecified());
  EXPECT_TRUE(sa1.is_un() == sa2.is_un());
  EXPECT_EQ(sa1.to_string(), sa2.to_string());
  EXPECT_EQ(sa1.family(), sa2.family());
  EXPECT_FALSE(sa1 != sa2);
  EXPECT_FALSE(sa1 < sa2);
  EXPECT_FALSE(sa2 < sa1);
}

TEST(SocketAddressDeathTest, DefaultConstruction) {

  // Default constructor.
  evio::SocketAddress sa1;
  CALL(test_is_unspecified(sa1));

  // Copy constructor.
  Debug(dc::warning.off());
  evio::SocketAddress sa2(sa1); // Prints: WARNING : Initializing a SocketAddress with an 'uninitialized' (default constructed) SocketAddress!
  Debug(dc::warning.on());
  CALL(test_equal(sa1, sa2));
}

// Statement expressions are not allowed at file scope. Therefore wrap them in a function.
uint16_t htons_(uint16_t in) { return htons(in); }
uint32_t htonl_(uint32_t in) { return htonl(in); }

struct sockaddr_in const sin_data = {
  AF_INET, htons_(65040), { htonl_((254 << 24)|(220 << 16)|(186 << 8)|152) }, { 0, }
};

struct sockaddr_in6 const sin6_data = {
  AF_INET6, htons_(65040), 0, {{{ 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10, 0x1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef }}}, 0
};

struct sockaddr_un const sun_data = {
  AF_UNIX, { '/', 's', 'o', 'm', 'e', '/', 'p', 'a', 't', 'h', 0, }
};

TEST(SocketAddressDeathTest, StructSockAddrConstructor) {
  // Construct a AF_INET SocketAddress.
  evio::SocketAddress sa1((struct sockaddr*)&sin_data);
  EXPECT_EQ(sa1.to_string(), "254.220.186.152:65040");
  CALL(test_is_ip4(sa1));

  // Copy constructor.
  evio::SocketAddress sa2(sa1);
  CALL(test_equal(sa1, sa2));

  // Construct AF_INET6 SocketAddress.
  evio::SocketAddress sa3((struct sockaddr*)&sin6_data);
  EXPECT_EQ(sa3.to_string(), "[fedc:ba98:7654:3210:123:4567:89ab:cdef]:65040");
  CALL(test_is_ip6(sa3));

  // Copy constructor.
  evio::SocketAddress sa4(sa3);
  CALL(test_equal(sa3, sa4));

  // Construct AF_UNIX SocketAddress.
  evio::SocketAddress sa5((struct sockaddr*)&sun_data);
  EXPECT_EQ(sa5.to_string(), "/some/path");
  CALL(test_is_un(sa5));

  // Copy constructor.
  evio::SocketAddress sa6(sa5);
  CALL(test_equal(sa5, sa6));
}

struct InOut {
  char const* input;
  char const* expected;
  sa_family_t family;
};

std::array<InOut, 28> const sockaddr_data = {{
  { "[2606:2800:220:1:248:1893:25c8:1946]:0", "[2606:2800:220:1:248:1893:25c8:1946]:0", AF_INET6 },
  { "[2001:41c0::645:a65e:60ff:feda:589d]:123", "[2001:41c0:0:645:a65e:60ff:feda:589d]:123", AF_INET6 },
  { "[2001:0db8::1:0:0:1]:12345", "[2001:db8::1:0:0:1]:12345", AF_INET6 },
  { "[2001:41c0::1]:65535", "[2001:41c0::1]:65535", AF_INET6 },
  { "[2606::1]:80", "[2606::1]:80", AF_INET6 },
  { "[::1]:1", "[::1]:1", AF_INET6 },
  { "[::]:9001", "[::]:9001", AF_INET6 },
  { "[::ffff:192:0:10:1]:1234", "[::ffff:192:0:10:1]:1234", AF_INET6 },
  { "::ffff:192.0.10.1:1234", "[::ffff:192.0.10.1]:1234", AF_INET6 },
  { "10.0.100.255:65535", "10.0.100.255:65535", AF_INET },
  { "[0010.000.0100.0255]:065535", "10.0.100.255:65535", AF_INET },
  { "[41c0::666a]:0", "[41c0::666a]:0", AF_INET6 },
  { "[::100.101.102.103]:0", "[::100.101.102.103]:0", AF_INET6 },
  { "[0:1::41c0]:0", "[0:1::41c0]:0", AF_INET6 },
  { "[41c0::1]:0", "[41c0::1]:0", AF_INET6 },
  { "[41c0::ffff]:0", "[41c0::ffff]:0", AF_INET6 },
  { "[::1]:0", "[::1]:0", AF_INET6 },
  { "[::1]:50007", "[::1]:50007", AF_INET6 },
  { "[::1]:65535", "[::1]:65535", AF_INET6 },
  { "[41c0::0]:0", "[41c0::]:0", AF_INET6 },
  { "[0::41c0]:0", "[::41c0]:0", AF_INET6 },
  { "[0::1:41c0]:0", "[::0.1.65.192]:0", AF_INET6 },
  { "[0:0::100.101.102.103]:0", "[::100.101.102.103]:0", AF_INET6 },
  { "[0:0::ffff:100.101.102.103]:0", "[::ffff:100.101.102.103]:0", AF_INET6 },
  { "[0::0:ffff:100.101.102.103]:0", "[::ffff:100.101.102.103]:0", AF_INET6 },
  { "[41c0::01]:0", "[41c0::1]:0", AF_INET6 },
  { "[41c0::00fffe]:0", "[41c0::fffe]:0", AF_INET6 },
  { "[::ffff:0.01.255.00]:0", "[::ffff:0.1.255.0]:0", AF_INET6 },
}};

TEST(SocketAddress, StringViewConstruction) {
  for (auto&& test_case : sockaddr_data)
  {
    evio::SocketAddress sa1(test_case.input);
    EXPECT_EQ(sa1.to_string(), test_case.expected);
    EXPECT_EQ(test_case.family, sa1.family());
  }
}

char const* const multiple_double_colon_error = "a double colon is only allowed once";
char const* const from_chars_failed_error = "std::from_chars failed";
char const* const colon_start_error = "IPv6 can only start with a colon if that is a double colon";
char const* const invalid_argument_error = "Invalid argument";
char const* const expected_colon_at_error = "expected ':' at";
char const* const ipv4_mapping_error = "IPv4 mapping only allowed after";
char const* const out_of_range_error = "Numerical result out of range";
char const* const missing_closing_bracket_error = "missing ']'";
char const* const missing_trailing_port_error = "missing trailing \":port\"";
char const* const trailing_characters_error = "trailing characters after port number";

std::array<InOut, 23> const faulty_sockaddr_data = {{
  { "[41c0::666a::]:0", multiple_double_colon_error, 0 },
  { "[41c0::666a::1]:0", multiple_double_colon_error, 0 },
  { "[41c0:::666a]:0", from_chars_failed_error, 0 },
  { "[:41c0::666a]:0", colon_start_error, 0 },
  { "[::41c0::666a]:0", multiple_double_colon_error, 0 },
  { "[0:0:ffff::100.101.102.103]:0", ipv4_mapping_error, 0 },
  { "[41c0:: 123]:0", invalid_argument_error, 0 },
  { " [41c0::123]:0", invalid_argument_error, 0 },
  { "[ 41c0::123]:0", invalid_argument_error, 0 },
  { "[41c0 ::123]:0", expected_colon_at_error, 0 },
  { "[::ffff:0.01.256.00]:0", out_of_range_error, 0 },
  { "[::ffff:10.1.a.0]:0", invalid_argument_error, 0 },
  { "[::ffff:10.1.-0.0]:0", invalid_argument_error, 0 },
  { "[::ffff:10.1.+1.0]:0", invalid_argument_error, 0 },
  { "[::ffff:10.1.1.0x0]:0", missing_closing_bracket_error, 0 },
  { "[::ffff:10.1. 123.42]:0", invalid_argument_error, 0 },
  { "[::1]", missing_trailing_port_error, 0 },
  { "[::1]:", invalid_argument_error, 0 },
  { "[::1]:0x0", trailing_characters_error, 0 },
  { "[::1]:-0", invalid_argument_error, 0 },
  { "[::1]:+1", invalid_argument_error, 0 },
  { "[::1]: 123", invalid_argument_error, 0 },
  { "[::1]:65536", out_of_range_error, 0 },
}};

TEST(SocketAddress, FaultyStringViewConstruction)
{
  for (auto&& test_case : faulty_sockaddr_data)
  {
    EXPECT_AIALERT(
      {
        evio::SocketAddress sa(test_case.input);
        FAIL() << "Input \"" << test_case.input << "\" does not throw but results in \"" << sa << "\".";
      }, test_case.expected);
  }
}

std::array<InOut, 4> const family_sockaddr_data = {{
  { "123.234.1.42:300", "123.234.1.42:300", AF_INET },
  { "123.234.1.42:300", "[::ffff:123.234.1.42]:300", AF_INET6 },
  { "123.234.1.42", "123.234.1.42", AF_UNIX },
  { "[::123.234.1.42]:300", "[::123.234.1.42]:300", AF_INET6 },
}};

TEST(SocketAddress, StringViewConstructionWithFamily)
{
  for (auto&& test_case : family_sockaddr_data)
  {
    evio::SocketAddress sa(test_case.family, test_case.input);
    EXPECT_EQ(sa.to_string(), test_case.expected);
    EXPECT_EQ(sa.family(), test_case.family);
  }
}

std::array<InOut, 2> const faulty_family_sockaddr_data = {{
  { "[::123.234.1.42]:300", "IPv4 can not start with a colon", AF_INET },
  { "/123.234.1.42:300", "-", AF_INET }
}};

TEST(SocketAddressDeathTest, FaultyStringViewConstructionWithFamily)
{
  for (auto&& test_case : faulty_family_sockaddr_data)
  {
    EXPECT_AIALERT(
      {
        if (*test_case.expected == '-')
        {
#ifdef CWDEBUG
          EXPECT_DEATH({ evio::SocketAddress sa(test_case.family, test_case.input); }, "^COREDUMP *:.*Assertion");
#else
          EXPECT_DEATH({ evio::SocketAddress sa(test_case.family, test_case.input); }, "Assertion .*failed");
#endif
        }
        else
        {
          evio::SocketAddress sa(test_case.family, test_case.input);
          FAIL() << "Input \"" << test_case.input << "\" does not throw but results in \"" << sa << "\".";
        }
      }, test_case.expected);
  }
}

TEST(SocketAddress, StringViewConstructionWithPort)
{
  evio::SocketAddress sa("1.2.3.4", 42);
  EXPECT_EQ(sa.to_string(), "1.2.3.4:42");
  EXPECT_AIALERT({ evio::SocketAddress sa("1.2.3.4:42", 42); }, "trailing characters after address");
}

TEST(SocketAddress, StringViewConstructionWithFamilyAndPort)
{
  evio::SocketAddress sa1(AF_INET, "192.168.10.1", 42);
  EXPECT_EQ(sa1.to_string(), "192.168.10.1:42");

  evio::SocketAddress sa2(AF_INET6, "192.168.10.1", 42);
  EXPECT_EQ(sa2.to_string(), "[::ffff:192.168.10.1]:42");
}
