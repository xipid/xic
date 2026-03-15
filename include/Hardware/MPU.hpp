#ifndef XI_HARDWARE_MPU_HPP
#define XI_HARDWARE_MPU_HPP

#include "../Xi/Device.hpp"
#include "../Xi/Math.hpp"

namespace Xi {

class MPUDevice : public Device {
public:
    Vector3 accel;
    Vector3 gyro;
    Vector3 mag;
    f32 temp;

    virtual void update() override = 0;
};

class MPU9250 : public MPUDevice {
public:
    u8 addr = 0x68;
    int cs = -1;
    bool useSPI = false;

    void init();
    void update() override;

private:
#if defined(ARDUINO)
    void writeReg(u8 reg, u8 val);
    void readRegs(u8 reg, u8 *buf, int len);
    void writeReg0xC(u8 reg, u8 val);
    void readRegs0xC(u8 reg, u8 *buf, int len);
#endif
};

} // namespace Xi

#endif
