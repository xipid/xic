#ifndef XI_HARDWARE_MPU_HPP
#define XI_HARDWARE_MPU_HPP

#include "../Input/Input.hpp"
#include "../Collection/Array.hpp"
#include "../Xi/Primitives.hpp"
#include "../Xi/Math.hpp"

namespace Xi {

class MPUDevice : public Input::InputDevice {
public:
    Vector3 accel = {0,0,0};
    Vector3 gyro = {0,0,0};
    Vector3 mag = {0,0,0};
    f32 temperature = 0;

    // Calibration offsets
    Vector3 accelOffset = {0,0,0};
    Vector3 gyroOffset = {0,0,0};
    Vector3 magOffset = {0,0,0};

    virtual void update() override = 0;
};

class MPU9250 : public MPUDevice {
public:
    MPU9250(u8 address = 0x68);
    
    void update() override;
    void calibrate();

private:
    u8 _addr;
};

} // namespace Xi

#endif
