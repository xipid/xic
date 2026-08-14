/**
 * @file Transform.hpp
 * @brief Spatial transforms, geodesy, and positioning for the Xi framework.
 */

#ifndef XI_CORE_TRANSFORM_HPP
#define XI_CORE_TRANSFORM_HPP

#include "../Collection/String.hpp"
#include "Math.hpp"
#include "Time.hpp"

namespace Xi {

/**
 * @struct Transform
 * @brief Base class for 3D spatial transforms (position, rotation, scale).
 */
struct XI_EXPORT Transform {
  virtual ~Transform() = default;

  Vector3 position = {0, 0, 0};
  Vector3 rotation = {0, 0, 0};
  Vector3 scale = {1, 1, 1};

  u32 transformVersion = 1;

  void update() {
    transformVersion++;
    if (transformVersion == 0)
      transformVersion = 1;
  }

  Matrix4 getMatrix() const;
  void lookAt(Vector3 target, Vector3 up = {0, 0, 1});

  mutable Matrix4 _cachedMatrix = identity();
  mutable u32 _cachedVersion = 0;
};

/**
 * @struct SphereConfig
 * @brief WGS84 Ellipsoid constants.
 */
struct XI_EXPORT SphereConfig {
  static constexpr f64 WGS84_A = 6378137.0;
  static constexpr f64 WGS84_F = 1.0 / 298.257223563;
  static constexpr f64 WGS84_B = WGS84_A * (1.0 - WGS84_F);
  static constexpr f64 WGS84_E2 =
      (WGS84_A * WGS84_A - WGS84_B * WGS84_B) / (WGS84_A * WGS84_A);
};

class XI_EXPORT GeoSpatial : public Transform {
public:
  /** @brief Sets position from Latitude (deg), Longitude (deg), Altitude (m).
   */
  void setGeoPos(Vector3 lla) {
    f64 lat = lla.x * (PI / 180.0);
    f64 lon = lla.y * (PI / 180.0);
    f64 alt = lla.z;

    f64 sinLat = sin((f32)lat);
    f64 cosLat = cos((f32)lat);
    f64 N = SphereConfig::WGS84_A /
            sqrt(1.0 - SphereConfig::WGS84_E2 * sinLat * sinLat);

    position.x = (f32)((N + alt) * cosLat * cos((f32)lon));
    position.y = (f32)((N + alt) * cosLat * sin((f32)lon));
    position.z = (f32)((N * (1.0 - SphereConfig::WGS84_E2) + alt) * sinLat);
    update();
  }

  Vector3 getGeoPosition() const {
    f64 x = position.x;
    f64 y = position.y;
    f64 z = position.z;

    f64 lon = atan2((f32)y, (f32)x);
    f64 p = sqrt(x * x + y * y);
    f64 lat = atan2((f32)z, (f32)(p * (1.0 - SphereConfig::WGS84_E2)));
    f64 h = 0;

    // Iterative refinement
    for (int i = 0; i < 5; ++i) {
      f64 sinLat = sin((f32)lat);
      f64 N = SphereConfig::WGS84_A /
              sqrt(1.0 - SphereConfig::WGS84_E2 * sinLat * sinLat);
      h = p / cos((f32)lat) - N;
      lat = atan2((f32)z,
                  (f32)(p * (1.0 - SphereConfig::WGS84_E2 * N / (N + h))));
    }

    return {(f32)(lat * (180.0 / PI)), (f32)(lon * (180.0 / PI)), (f32)h};
  }

  int getGMT() const {
    Vector3 lla = getGeoPosition();
    return (int)round(lla.y / 15.0f) * 3600;
  }
};

struct TimedTransformMeasurement {
  Vector3 velocity = {0, 0, 0};
  Vector3 angularVelocity = {0, 0, 0};

  u64 lastMicros = 0;
  f32 dt = 0;

  void sync(u64 nowMicros) {
    if (lastMicros != 0)
      dt = (f32)(nowMicros - lastMicros) / 1000000.0f;
    lastMicros = nowMicros;
  }
};

class XI_EXPORT TimedGeoSpatial : public GeoSpatial,
                                  public TimedTransformMeasurement {
public:
  Time time() const { return Time(lastMicros, getGMT()); }
};

class XI_EXPORT Enviromental {
public:
  f32 temperature = 0;
  f32 pressure = 0;
  f32 humidity = 0;
};

class XI_EXPORT TimedGeoEnviromental : public TimedGeoSpatial,
                                       public Enviromental {};

namespace Internal {
inline TimedGeoEnviromental &defaultLocation() {
  static TimedGeoEnviromental inst;
  return inst;
}

inline TimedGeoEnviromental *&locationPtr() {
  static TimedGeoEnviromental *ptr = &defaultLocation();
  return ptr;
}
} // namespace Internal

#define location (*::Xi::Internal::locationPtr())

} // namespace Xi

#endif // XI_CORE_TRANSFORM_HPP
