#pragma once

#include "evio/InputDevice.h"
#include "evio/OutputDevice.h"

class NoEpollInputDevice : public evio::InputDevice
{
 public:
  using VT_type = evio::InputDevice::VT_type;

  struct VT_impl : evio::InputDevice::VT_impl
  {
    static NAD_DECL_UNUSED_ARG(read_from_fd, evio::InputDevice* UNUSED_ARG(self), int UNUSED_ARG(fd))
    {
    }

    static constexpr VT_type VT{
      /*InputDevice*/
      nullptr,
      read_from_fd,             // override
      hup,
      exceptional,
      nullptr,
      nullptr,
      nullptr,
    };
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<NoEpollInputDevice, evio::InputDevice> VT_ptr;

  NoEpollInputDevice() : VT_ptr(this) { }
};

class NoEpollOutputDevice : public evio::OutputDevice
{
 public:
  using VT_type = evio::OutputDevice::VT_type;

  struct VT_impl : evio::OutputDevice::VT_impl
  {
    static NAD_DECL_UNUSED_ARG(write_to_fd, OutputDevice* UNUSED_ARG(self), int UNUSED_ARG(fd))
    {
    }

    static constexpr VT_type VT{
      /*OutputDevice*/
      nullptr,
      write_to_fd,              // override
      nullptr
    };
  };

  VT_type* clone_VT() override { return VT_ptr.clone(this); }
  utils::VTPtr<NoEpollOutputDevice, evio::OutputDevice> VT_ptr;

  NoEpollOutputDevice() : VT_ptr(this) { }
};
