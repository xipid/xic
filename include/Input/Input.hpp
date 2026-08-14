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

struct XI_EXPORT Input {
    u32 id = 0;
    Collection::String key;

    f32 value = 0.0f;
    f32 prevValue = 0.0f;
    bool downActive = false;
    bool upActive = false;

    virtual bool down() { return downActive; }
    virtual bool up() { return upActive; }

    virtual void update() {
        if (value == 0.0f && prevValue != 0.0f) {
            downActive = true;
        } else {
            downActive = false;
        }
        if (value != 0.0f && prevValue == 0.0f) {
            upActive = true;
        } else {
            upActive = false;
        }
        prevValue = value;
    }

    virtual ~Input() = default;
};

struct XI_EXPORT Input2 : public Input {
    f32 value2 = 0.0f;
    f32 prevValue2 = 0.0f;
    bool downActive2 = false;
    bool upActive2 = false;

    virtual bool down2() { return downActive2; }
    virtual bool up2() { return upActive2; }

    void update() override {
        Input::update();
        if (value2 == 0.0f && prevValue2 != 0.0f) {
            downActive2 = true;
        } else {
            downActive2 = false;
        }
        if (value2 != 0.0f && prevValue2 == 0.0f) {
            upActive2 = true;
        } else {
            upActive2 = false;
        }
        prevValue2 = value2;
    }
};

struct XI_EXPORT Input3 : public Input2 {
    f32 value3 = 0.0f;
    f32 prevValue3 = 0.0f;
    bool downActive3 = false;
    bool upActive3 = false;

    virtual bool down3() { return downActive3; }
    virtual bool up3() { return upActive3; }

    void update() override {
        Input2::update();
        if (value3 == 0.0f && prevValue3 != 0.0f) {
            downActive3 = true;
        } else {
            downActive3 = false;
        }
        if (value3 != 0.0f && prevValue3 == 0.0f) {
            upActive3 = true;
        } else {
            upActive3 = false;
        }
        prevValue3 = value3;
    }
};

} // namespace Input

#endif // XI_INPUT_INPUT_HPP
