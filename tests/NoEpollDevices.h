#pragma once

#include "evio/InputDevice.h"
#include "evio/OutputDevice.h"

class NoEpollInputDevice : public evio::InputDevice
{
 public:
  using VT_type = evio::InputDevice::VT_type;
  #define VT_NoEpollInputDevice VT_evio_InputDevice

  struct VT_impl : evio::InputDevice::VT_impl
  {
    static NAD_DECL_UNUSED_ARG(read_from_fd, evio::InputDevice* UNUSED_ARG(self), int UNUSED_ARG(fd))
    {
    }

    // These should never be called.
    static constexpr std::nullptr_t read_returned_zero = nullptr;
    static constexpr std::nullptr_t read_error = nullptr;
    static constexpr std::nullptr_t data_received = nullptr;

    static constexpr VT_type VT VT_NoEpollInputDevice;
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<NoEpollInputDevice, evio::InputDevice> VT_ptr;

  NoEpollInputDevice() : VT_ptr(this) { }
};

class NoEpollOutputDevice : public evio::OutputDevice
{
 public:
  using VT_type = evio::OutputDevice::VT_type;
  #define VT_NoEpollOutputDevice VT_evio_OutputDevice

  struct VT_impl : evio::OutputDevice::VT_impl
  {
    static NAD_DECL_UNUSED_ARG(write_to_fd, OutputDevice* UNUSED_ARG(self), int UNUSED_ARG(fd))
    {
    }

    // These should never be called.
    static constexpr std::nullptr_t write_error = nullptr;

    static constexpr VT_type VT VT_NoEpollOutputDevice;
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<NoEpollOutputDevice, evio::OutputDevice> VT_ptr;

  NoEpollOutputDevice() : VT_ptr(this) { }
};
