#include <Xi/Device.hpp>
#include <Xi/String.hpp>

namespace Xi {

DeviceScreen* Device::screen() { return get<DeviceScreen>(); }
DeviceRenderingDevice* Device::renderingDevice() { return get<DeviceRenderingDevice>(); }

void* DeviceScreen::deviceView(i32 type) {
    if (surface) return surface->deviceView(type);
    return nullptr;
}

} // namespace Xi
