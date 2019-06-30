#pragma once

class MyDummyDecoder : public evio::InputDecoder
{
 protected:
  NAD_DECL_UNUSED_ARG(decode, evio::MsgBlock&& UNUSED_ARG(msg)) override { }
};
