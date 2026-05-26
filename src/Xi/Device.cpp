/**
 * @file Device.cpp
 * @brief Implementation of the base Device class and its shorthand methods.

 */

#include "../../include/Xi/Device.hpp"

namespace Xi {

DeviceScreen *Device::screen() { return get<DeviceScreen>(); }

DeviceRenderingDevice *Device::renderingDevice() {
  return get<DeviceRenderingDevice>();
}

void *DeviceScreen::deviceView(i32 type) {
  if (surface)
    return surface->deviceView(type);
  return nullptr;
}

} // namespace Xi