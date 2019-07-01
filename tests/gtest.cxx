#include "sys.h"
#include "gtest.h"
#include "debug.h"
#include <boost/program_options.hpp>

#if 0
//#include "test_set_XXXsockbuf.h"
#include "test_print_hostent_on.h"
#include "test_size_of_addr.h"
#endif
#include "test_SocketAddress.h"
#if 0
#include "test_RefCountReleaser.h"
#ifdef CWDEBUG  // Only compiles with CWDEBUG defined.
#include "test_FileDescriptor.h"
#endif
#include "test_IODevice.h"
#include "test_InputDecoder.h"
#include "test_OutputStream.h"
#include "test_StreamBuf.h"
#endif
//#include "test_Socket.h"

using namespace boost::program_options;

bool g_debug_output_on;

int main(int argc, char* argv[])
{
  Debug(debug::init());
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::UnitTest::GetInstance()->listeners().Append(new GtestThrowListener);

  try
  {
    options_description desc{"Options"};
    desc.add_options()
      ("help,h", "Help screen")
      ("debug-output,d", "Turn on normal debug output.")
      ;

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("help"))
    {
      std::cout << desc << '\n';
      return 0;
    }
    g_debug_output_on = vm.count("debug-output") > 0;
  }
  catch (error const& ex)
  {
    std::cerr << ex.what() << '\n';
  }

  Debug(if (!g_debug_output_on)
        {
          libcw_do.off();
          NAMESPACE_DEBUG::thread_init_default = libcwd::debug_off;
        }
      );
  Dout(dc::notice, "Debug output is turned on.");

  return RUN_ALL_TESTS();
}
