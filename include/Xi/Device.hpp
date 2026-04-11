/**
 * @file Device.hpp
 * @brief Base abstractions for hardware and software devices in the Xi
 * framework.

 */

#ifndef XI_CORE_DEVICE_HPP
#define XI_CORE_DEVICE_HPP

#include "../Collection/Array.hpp"
#include "../Collection/String.hpp"

using namespace Collection;

namespace Xi {

class DeviceScreen;
class DeviceRenderingDevice;

/**
 * @class Device
 * @brief Base class for all physical and virtual devices (IO, Windows, Sockets,
 * etc.).
 */
class XI_EXPORT Device {
public:
  String name = "Device";  ///< Human-readable name of the device.
  Array<Device *> devices; ///< Sub-devices attached to this device.
  bool shouldRelease =
      false; ///< Flag to indicate if the device should be destroyed.
  bool outputIntended =
      true; ///< Flag to indicate if the device is intended for output.

  virtual ~Device() = default;

  /** @brief Updates the device state (called once per frame/loop). */
  virtual void update() {}

  /**
   * @brief Retrieves a sub-device by its type.
   * @tparam T The class type of the device to find.
   * @return Pointer to the device, or nullptr if not found.
   */
  template <typename T> T *get() {
    for (usz i = 0; i < devices.size(); ++i) {
      if (auto *d = dynamic_cast<T *>(devices[i]))
        return d;
    }
    return nullptr;
  }

  /** @brief Shorthand to retrieve the first available DeviceScreen. */
  DeviceScreen *screen();

  /** @brief Shorthand to retrieve the first available DeviceRenderingDevice. */
  DeviceRenderingDevice *renderingDevice();
};

/**
 * @class MemoryDevice
 * @brief Abstract device that provides memory allocation and data transfer
 * capabilities.
 */
class XI_EXPORT MemoryDevice : public Device, public IMemoryDevice {
public:
  MemoryDevice() {
    name = "MemoryDevice";
    outputIntended = false;
  }

  virtual void *alloc(usz size) override = 0;
  virtual void free(void *handle) override = 0;
  virtual void upload(void *handle, const void *src, usz size) override = 0;
  virtual void download(void *handle, void *dst, usz size) override = 0;

  /** @brief Gets a CPU-accessible view of the memory handle. */
  virtual void *view(void *handle, i32 type = 0) override {
    (void)type;
    return handle;
  }

  /** @brief Allocates a 2D surface (texture) memory. */
  virtual void *allocSurface(i32 w, i32 h, i32 channels = 4) override {
    return alloc((usz)(w * h * channels));
  }

  virtual ~MemoryDevice() = default;
};

/**
 * @class Device1D
 * @brief A device representing a single floating-point value input/output.
 */
class XI_EXPORT Device1D : public Device {
public:
  f32 value = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Device1D() { outputIntended = false; }
};

/**
 * @class Device2D
 * @brief A device representing a 2D coordinate input/output.
 */
class XI_EXPORT Device2D : public Device {
public:
  f32 x = 0.0f, y = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Device2D() { outputIntended = false; }
};

/**
 * @class Device3D
 * @brief A device representing a 3D coordinate input/output.
 */
class XI_EXPORT Device3D : public Device {
public:
  f32 x = 0.0f, y = 0.0f, z = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Device3D() { outputIntended = false; }
};

/**
 * @class DeviceRenderingDevice
 * @brief Bridge between Xi Device system and GPU rendering hardware.
 */
class XI_EXPORT DeviceRenderingDevice : public MemoryDevice {
public:
  void *device = nullptr; ///< Pointer to the underlying hardware device.

  DeviceRenderingDevice() {
    name = "RenderingDevice";
    outputIntended = false;
  }

  virtual void *alloc(usz) override { return nullptr; }
  virtual void free(void *) override {}
  virtual void upload(void *, const void *, usz) override {}
  virtual void download(void *, void *, usz) override {}
};

/**
 * @class DeviceScreen
 * @brief Represents a display surface or window.
 */
class XI_EXPORT DeviceScreen : public Device {
public:
  i32 width = 0, height = 0;             ///< Dimensions of the screen.
  i32 screenWidth = 0, screenHeight = 0; ///< Total system screen dimensions.
  String title = "Xi Screen";            ///< Window title.

  bool writable = false; ///< Whether the screen can be directly written to.

  DeviceRenderingDevice *renderingDevice =
      nullptr;               ///< Associated GPU rendering device.
  String *surface = nullptr; ///< Pixel surface data.

  /** @brief Gets a view of the underlying OS-specific window object. */
  virtual void *deviceView(i32 type = 0);
};

} // namespace Xi

#endif // XI_CORE_DEVICE_HPP