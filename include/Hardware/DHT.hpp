#ifndef XI_HARDWARE_DHT_HPP
#define XI_HARDWARE_DHT_HPP

#include "../Xi/Device.hpp"
#include "../Xi/Primitives.hpp"

namespace Xi {

class DHTDevice : public Device {
public:
    f32 temperature = 0;
    f32 humidity = 0;

    virtual void update() override = 0;
};

class DHTImplementation : public DHTDevice {
public:
    DHTImplementation(int pin, int type = 22);
    void update() override;

private:
    int _pin, _type;
    u64 _lastRead = 0;
};

} // namespace Xi

#endif
