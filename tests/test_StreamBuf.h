#include "evio/StreamBuf.h"

TEST(StreamBuf, TypeDefs) {

  // Typedefs.
  using StreamBuf = evio::StreamBuf;
  EXPECT_TRUE((std::is_same<StreamBuf::char_type, char>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::traits_type, std::char_traits<char>>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::int_type, int>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::pos_type, std::streampos>::value));
  EXPECT_TRUE((std::is_same<StreamBuf::off_type, std::streamoff>::value));
}

TEST(StreamBuf, Constructor) {
  evio::StreamBuf* sb1 = new evio::StreamBuf(0, 0, 0);
  EXPECT_TRUE(sb1 != nullptr);

  delete sb1;
}
