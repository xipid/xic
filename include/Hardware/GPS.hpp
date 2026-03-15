#ifndef XI_HARDWARE_GPS_HPP
#define XI_HARDWARE_GPS_HPP

#include "../Xi/Device.hpp"
#include "../Xi/Spatial.hpp"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace Xi {

class GPSDevice : public Device, public GeodeticSphere {
public:
    GeoPos pos = {0, 0, 0};
    u64 lastTimeSync = 0; // Nanoseconds epoch
    bool hasFix = false;

#if defined(ARDUINO)
    Stream *port = nullptr;
#endif

    void update() override;
    void onPPS();

private:
    char gpsBuf[90] = {};
    int gpsIdx = 0;
    u64 lastPPS = 0;

    void parseNMEA(const char *buf);
};

} // namespace Xi

#endif
