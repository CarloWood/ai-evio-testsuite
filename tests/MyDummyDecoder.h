#pragma once

class MyDummyDecoder : public evio::protocol::Decoder
{
 protected:
  void decode(int& UNUSED_ARG(allow_deletion_count), evio::MsgBlock&& UNUSED_ARG(msg)) override { }
};
