#include "evio/inet_support.h"
#include "utils/nearest_power_of_two.h"
#include "utils/is_power_of_two.h"
#include <fstream>
#include <sys/socket.h>

// The type used by getsockopt for the option buffer when optname is SO_SNDBUF.
// Change the type of 'opt' in evio::set_sndsockbuf and evio::set_rcvsockbuf if
// this needs changing!
using opt_buf_size_t = int;

enum snd_or_rcv {
  sndbuf,
  rcvbuf
};

// Call function under test.
static void set_sockbuf(snd_or_rcv what, int sock_fd, size_t buf_size, size_t minimum_block_size)
{
  // A warning is expected when requested_buf_size is not a power of two, but it should still work (as long as requested_buf_size is even).
  if (!utils::is_power_of_two(buf_size))
    Debug(dc::warning.off());
  Debug(dc::notice.off());
  if (what == sndbuf)
    evio::set_sndsockbuf(sock_fd, buf_size, minimum_block_size);
  else
    evio::set_rcvsockbuf(sock_fd, buf_size, minimum_block_size);
  Debug(dc::notice.on());
  if (!utils::is_power_of_two(buf_size))
    Debug(dc::warning.on());
}

static opt_buf_size_t get_buf_size_len(snd_or_rcv what, int fd)
{
  opt_buf_size_t buf_size;
  socklen_t optlen = sizeof(buf_size);
  if (getsockopt(fd, SOL_SOCKET, what == sndbuf ? SO_SNDBUF : SO_RCVBUF, &buf_size, &optlen) != 0)
    THROW_ALERTE("testsuite: Unexepted failure of call to getsockopt(2)");
  // Sanity check.
  if (optlen != sizeof(buf_size))
    THROW_ALERT("testsuite: opt_buf_size_t has the wrong size?!");
  return buf_size;
}

static void run_test(snd_or_rcv what)
{
  ASSERT_NO_AIALERT_BEGIN
  {
    // Set up.
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    opt_buf_size_t mem_default, mem_max;
    std::ifstream(what == sndbuf ? "/proc/sys/net/core/wmem_default" : "/proc/sys/net/core/rmem_default") >> mem_default;
    std::ifstream(what == sndbuf ? "/proc/sys/net/core/wmem_max" : "/proc/sys/net/core/rmem_max") >> mem_max;
    // The actual maximum is twice the value we read from /proc.
    opt_buf_size_t const max_buf_size = mem_max * 2;

    // Test: use a very small SO_SNDBUF.
    set_sockbuf(what, fd, 16, 0);

    // Read back the actual SO_SNDBUF size.
    opt_buf_size_t const min_buf_size = get_buf_size_len(what, fd);

    // Print the minimal and maximal value that were obtained.
    Dout(dc::notice, "min_buf_size = 0x" << std::hex << min_buf_size << " bytes.");
    Dout(dc::notice, "max_buf_size = 0x" << std::hex << max_buf_size << " bytes.");

    // The value of 16 should not have been accepted. The kernel will enforce a much larger value.
    EXPECT_LT(16, min_buf_size);
    // But it should be (much) smaller than the usual default (which is normally equal to the maximum value).
    EXPECT_LT(min_buf_size, mem_default);

    bool done = false;
    // Start with a value that is less than min_buf_size.
    // base_buf_size is a power of two.
    for (opt_buf_size_t base_buf_size = utils::nearest_power_of_two(min_buf_size / 2);;)
    {
      // Also try just below and just above the power of two value.
      for (int offset = -2; offset <= 2; offset += 2)
      {
        opt_buf_size_t requested_buf_size = base_buf_size + offset;

        // Test: try various values of SO_SNDBUF.
        set_sockbuf(what, fd, requested_buf_size, 0);  // The value 0 is not used.

        // Read the value back.
        opt_buf_size_t actual_buf_size = get_buf_size_len(what, fd);
        Dout(dc::notice, "When requesting 0x" << std::hex << requested_buf_size << " the actual buf_size size becomes 0x" << std::hex << actual_buf_size << " bytes.");

        // It would be very strange if suddenly the actual buf_size size would be less than what we just assumed to a minimum.
        EXPECT_GE(actual_buf_size, min_buf_size);

        // The resulting SO_SNDBUF should always be larger or equal the requested size,
        // unless it was larger than twice the maximum size.
        ASSERT_GE(actual_buf_size, std::min(requested_buf_size, max_buf_size));

        // If the returned size is less than what we asked for than it should be equal to the maximum value.
        if (actual_buf_size < requested_buf_size)
        {
          ASSERT_EQ(actual_buf_size, max_buf_size);
          done = true;
          break;
        }

        // If the actual size isn't the minimum value than we expect it to be equal to what we requested (this is not necessary, but currently the case).
        if (min_buf_size < actual_buf_size && actual_buf_size < max_buf_size)
        {
          EXPECT_EQ(requested_buf_size, actual_buf_size);
        }
      }

      // Break out of both loops.
      if (done)
        break;

      // We should never reach this maximum value.
      ASSERT_LT(base_buf_size, max_buf_size);

      opt_buf_size_t prev_buf_size = base_buf_size;
      base_buf_size *= 2;

      // Make sure we're not overflowing.
      ASSERT_GT(base_buf_size, prev_buf_size);
    }
  }
  ASSERT_NO_AIALERT_END;
}

TEST(inet_support, set_sndsockbuf) {
  run_test(sndbuf);
}

TEST(inet_support, set_rcvsockbuf) {
  run_test(rcvbuf);
}
