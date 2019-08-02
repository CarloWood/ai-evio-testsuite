#pragma once

#include "evio/InputDevice.h"
#include "evio/OutputDevice.h"

class NoEpollInputDevice : public evio::InputDevice
{
 public:
  void read_from_fd(int& UNUSED_ARG(allow_deletion_count), int UNUSED_ARG(fd)) override { }

  // These should never be called.
  void read_returned_zero(int& UNUSED_ARG(allow_deletion_count)) { ASSERT(false); }
  void read_error        (int& UNUSED_ARG(allow_deletion_count), int UNUSED_ARG(err)) { ASSERT(false); }
  void data_received     (int& UNUSED_ARG(allow_deletion_count), char const* UNUSED_ARG(new_data), size_t UNUSED_ARG(rlen)) { ASSERT(false); }
};

class NoEpollOutputDevice : public evio::OutputDevice
{
 public:
  void write_to_fd(int& UNUSED_ARG(allow_deletion_count), int UNUSED_ARG(fd)) override { }

  // These should never be called.
  void write_error(int& UNUSED_ARG(allow_deletion_count), int UNUSED_ARG(err)) { ASSERT(false); }
};
