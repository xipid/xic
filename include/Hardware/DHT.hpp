#ifndef XI_HARDWARE_DHT_HPP
#define XI_HARDWARE_DHT_HPP

#include "../Xi/Device.hpp"

namespace Xi {

class DHTDevice : public Device {
public:
    f32 temperature = 0;
    f32 humidity = 0;
    int pin = -1;
    int type = 22; // DHT22 default

    void update() override;

private:
    unsigned long lastRead = 0;

#if defined(ARDUINO)
    void captureData();
#endif
};

} // namespace Xi

#endif
