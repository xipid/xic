#ifndef XI_INPUT_INPUT_HPP
#define XI_INPUT_INPUT_HPP

#include <Xi/Primitives.hpp>
#include <Collection/String.hpp>

namespace Input {

class XI_EXPORT InputDevice {
public:
  Collection::String name = "InputDevice";
  virtual void update() {}
  virtual ~InputDevice() = default;
};

class XI_EXPORT Input1D : public InputDevice {
public:
  f32 value = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Input1D() { name = "Input1D"; }
};

class XI_EXPORT Input2D : public InputDevice {
public:
  f32 x = 0.0f, y = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Input2D() { name = "Input2D"; }
};

class XI_EXPORT Input3D : public InputDevice {
public:
  f32 x = 0.0f, y = 0.0f, z = 0.0f;
  i32 id = 0;
  bool readable = false;
  bool writable = false;
  Input3D() { name = "Input3D"; }
};

} // namespace Input

#endif // XI_INPUT_INPUT_HPP
