#ifndef XI_HARDWARE_SPATIAL_HPP
#define XI_HARDWARE_SPATIAL_HPP

#include "../Collection/Array.hpp"
#include "../Xi/Transform.hpp"
#include "DHT.hpp"
#include "GPS.hpp"
#include "MPU.hpp"

namespace Xi {

/**
 * @class HardwareSpatial
 * @brief Unified hardware source for positioning, orientation, and environment.
 * 
 * Inherits from TimedGeoEnviromental to provide a single point of truth
 * for GPS (GeoSpatial), IMU (TimedMeasurement), and DHT (Enviromental).
 */
class HardwareSpatial : public TimedGeoEnviromental {
public:
  static HardwareSpatial &getInstance();

  Array<MPUDevice *> mpu;
  Array<GPSDevice *> gps;
  Array<DHTDevice *> dht;

  /** @brief Performs high-frequency sensor fusion and device updates. */
  void update();

  // Fusion & Orientation State
  Vector3 down = {0, 0, 1};
  Vector3 north = {0, 1, 0};
  Vector3 deltaPos = {0, 0, 0};
  Vector3 deltaRotation = {0, 0, 0};
  u64 realTime = 0;

private:
  HardwareSpatial();

  // Mahony Fusion State (Quaternions)
  float q0, q1, q2, q3;
  float eInt[3] = {0, 0, 0};

  u64 timeOffsetNS = 0;

  u64 getSystemTimeNS();
  void syncTime(u64 nowSys);
  void fuseSensors(float dt);
};

/** @brief Global reference to the singleton instance. */
static HardwareSpatial &space = HardwareSpatial::getInstance();

} // namespace Xi

#endif
