#ifndef XI_DEVICE_HPP
#define XI_DEVICE_HPP

#include "Array.hpp"
#include "String.hpp"

namespace Xi {

class DeviceScreen;
class DeviceRenderingDevice;

// -------------------------------------------------------------------------
// Device — Base class for all devices (IO, Windows, Filesystems)
// -------------------------------------------------------------------------
class XI_EXPORT Device {
public:
  String name = "Device";
  Array<Device *> devices;
  bool shouldRelease = false;
  bool outputIntended = true;

  virtual ~Device() = default;
  virtual void update() {}

  /// Get a device by type
  template <typename T> T *get() {
    for (usz i = 0; i < devices.length(); ++i) {
      if (auto *d = dynamic_cast<T *>(devices[i]))
        return d;
    }
    return nullptr;
  }

  /// Shorthand: first DeviceScreen
  DeviceScreen *screen();

  /// Shorthand: first DeviceRenderingDevice
  DeviceRenderingDevice *renderingDevice();
};

// -------------------------------------------------------------------------
// MemoryDevice — Represents a device that provides memory (IMemoryDevice)
// -------------------------------------------------------------------------
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

  virtual void *view(void *handle, i32 type = 0) override {
    (void)type;
    return handle;
  }

  virtual void *allocSurface(i32 w, i32 h, i32 channels = 4) override {
    return alloc((usz)(w * h * channels));
  }

  virtual ~MemoryDevice() = default;
};

// -------------------------------------------------------------------------
// Specialized Base Devices
// -------------------------------------------------------------------------

class XI_EXPORT Device1D : public Device {
public:
  f32 value = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Device1D() { outputIntended = false; }
};

class Device2D : public Device {
public:
  f32 x = 0.0f, y = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Device2D() { outputIntended = false; }
};

class Device3D : public Device {
public:
  f32 x = 0.0f, y = 0.0f, z = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Device3D() { outputIntended = false; }
};

// -------------------------------------------------------------------------
// DeviceRenderingDevice — Wraps a GPU rendering device
// -------------------------------------------------------------------------
class DeviceRenderingDevice : public MemoryDevice {
public:
  void *device = nullptr; // e.g. Diligent::IRenderDevice*

  DeviceRenderingDevice() {
    name = "RenderingDevice";
    outputIntended = false;
  }

  void *alloc(usz) override { return nullptr; }
  void free(void *) override {}
  void upload(void *, const void *, usz) override {}
  void download(void *, void *, usz) override {}
};

// -------------------------------------------------------------------------
// DeviceScreen — Represents a display surface
// -------------------------------------------------------------------------
class DeviceScreen : public Device {
public:
  i32 width = 0, height = 0;
  i32 screenWidth = 0, screenHeight = 0;
  String title = "Xi Screen";

  bool writable = false;

  DeviceRenderingDevice *renderingDevice = nullptr;
  String *surface = nullptr;

  virtual void *deviceView(i32 type = 0);
};

} // namespace Xi

#endif // XI_DEVICE_HPP
