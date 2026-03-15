#ifndef XI_HARDWARE_SPATIAL_HPP
#define XI_HARDWARE_SPATIAL_HPP

#include "../Xi/Spatial.hpp"
#include "../Xi/Array.hpp"
#include "MPU.hpp"
#include "GPS.hpp"
#include "DHT.hpp"

namespace Xi {

class HardwareSpatial : public Spatial {
public:
    static HardwareSpatial &getInstance();

    Array<MPUDevice*> mpu;
    Array<GPSDevice*> gps;
    Array<DHTDevice*> dht;

    void update() override;

private:
    HardwareSpatial();

    // Fusion state
    float q0, q1, q2, q3;
    float eInt[3] = {0, 0, 0};
    
    u64 timeOffsetNS = 0;

    u64 getSystemTimeNS();
    void syncTime(u64 nowSys);
    void fuseSensors(float dt);
};

static HardwareSpatial &space = HardwareSpatial::getInstance();

} // namespace Xi

#endif
