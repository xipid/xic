#ifndef XI_SPATIAL_HPP
#define XI_SPATIAL_HPP

#include "Math.hpp"

namespace Xi {

struct XI_EXPORT Transform {
  const Vector3 &getPosition() const { return _position; }
  const Vector3 &getRotation() const { return _rotation; }
  const Vector3 &getScale() const { return _scale; }

  void setPosition(const Vector3 &p) {
    _position = p;
    touch();
  }
  void setRotation(const Vector3 &r) {
    _rotation = r;
    touch();
  }
  void setScale(const Vector3 &s) {
    _scale = s;
    touch();
  }

  Vector3 &position() {
    touch();
    return _position;
  }
  Vector3 &rotation() {
    touch();
    return _rotation;
  }
  Vector3 &scale() {
    touch();
    return _scale;
  }

  u32 transformVersion = 1;

  void touch() {
    transformVersion++;
    if (transformVersion == 0)
      transformVersion = 1;
  }

  Matrix4 getMatrix() const;
  void lookAt(Vector3 target, Vector3 up = {0, 1, 0});

protected:
  Vector3 _position = {0, 0, 0};
  Vector3 _rotation = {0, 0, 0};
  Vector3 _scale = {1, 1, 1};

  mutable Matrix4 _cachedMatrix = Math::identity();
  mutable u32 _cachedVersion = 0;
};

struct SphereConfig {
  f32 a; // Semi-major axis (e.g., 6378137.0 for Earth)
  f32 f; // Flattening (e.g., 1/298.257223563 for Earth)
};

struct GeoPos {
  f32 lat, lng, alt;
};

class XI_EXPORT GeodeticSphere {
public:
  SphereConfig config;

  GeodeticSphere();
  GeoPos getGeoPos(const Transform &t) const;
};

class XI_EXPORT Spatial : public Transform {
public:
  GeodeticSphere sphere;

  Vector3 north;
  Vector3 down; // Gravity vector

  // position, rotation inherited

  Vector3 deltaPos; // acceleration
  Vector3 deltaRotation;

  // Low-level timing
  u64 lastMeasurement; // nanoseconds since epoch
  u64 realTime;        // current synced epoch time in nanoseconds
  f32 deltaTime;

  virtual void update() = 0;
};

} // namespace Xi

#endif
