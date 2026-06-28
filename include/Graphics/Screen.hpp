#ifndef XI_GRAPHICS_SCREEN_HPP
#define XI_GRAPHICS_SCREEN_HPP

#include <Xi/Primitives.hpp>
#include <Collection/String.hpp>
#include <Input/Input.hpp>

namespace Graphics {

class XI_EXPORT Screen {
public:
  i32 width = 0, height = 0;
  i32 screenWidth = 0, screenHeight = 0;
  Collection::String title = "Xi Screen";
  bool writable = false;
  Xi::MemoryDevice *gpu = nullptr; // GPU rendering device
  Collection::String *surface = nullptr; // pixel surface data

  virtual void update() {}
  virtual void* deviceView(i32 type = 0) {
    if (surface)
      return surface->deviceView(type);
    return nullptr;
  }
  virtual ~Screen() = default;
};

} // namespace Graphics

#endif // XI_GRAPHICS_SCREEN_HPP
