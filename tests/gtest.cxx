#include "sys.h"
#include "gtest.h"
#include "debug.h"
#include <boost/program_options.hpp>

#if 0
#include "test_set_XXXsockbuf.h"
#include "test_print_hostent_on.h"
#include "test_size_of_addr.h"
#include "test_SocketAddress.h"
#include "test_RefCountReleaser.h"
#endif
#include "test_FileDescriptor.h"
//#include "test_StreamBuf.h"

using namespace boost::program_options;

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

    Debug(libcw_do.off());
    if (vm.count("help"))
    {
      std::cout << desc << '\n';
      return 0;
    }
    Debug(if (vm.count("debug-output")) libcw_do.on());
  }
  catch (error const& ex)
  {
    std::cerr << ex.what() << '\n';
  }

  return RUN_ALL_TESTS();
}
