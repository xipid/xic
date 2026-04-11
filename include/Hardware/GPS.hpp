#ifndef XI_HARDWARE_GPS_HPP
#define XI_HARDWARE_GPS_HPP

#include "../Xi/Device.hpp"
#include "../Collection/Array.hpp"
#include "../Xi/Math.hpp"

namespace Xi {

class GPSDevice : public Device {
public:
    Vector3 pos = {0, 0, 0}; ///< Latitude, Longitude, Altitude
    u64 lastTimeSync = 0;    ///< Last synchronized epoch microseconds
    bool hasFix = false;

    virtual void update() override = 0;

    static Vector3 toDegrees(Vector3 lla) {
        return {(f32)(lla.x * (180.0 / PI)), (f32)(lla.y * (180.0 / PI)), (f32)lla.z};
    }

    static Vector3 toRadians(Vector3 lla) {
        return {(f32)(lla.x * (PI / 180.0)), (f32)(lla.y * (PI / 180.0)), (f32)lla.z};
    }
};

class UbloxGPS : public GPSDevice {
public:
    UbloxGPS(int rx, int tx, int pps = -1);
    
    void update() override;

private:
    int _rx, _tx, _pps;
};

} // namespace Xi

#endif
