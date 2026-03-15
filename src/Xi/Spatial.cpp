#include "../../include/Xi/Spatial.hpp"

namespace Xi {

Matrix4 Transform::getMatrix() const {
  if (transformVersion != _cachedVersion) {
    _cachedMatrix = Math::rotateX(_rotation.x) * Math::rotateY(_rotation.y) *
                    Math::translate(_position.x, _position.y, _position.z);
    _cachedVersion = transformVersion;
  }
  return _cachedMatrix;
}

void Transform::lookAt(Vector3 target, Vector3 up) {
  Vector3 direction = {target.x - _position.x, target.y - _position.y,
                       target.z - _position.z};
  float horizontalDistance =
      Xi::Math::sqrt(direction.x * direction.x + direction.z * direction.z);
  _rotation.x = -Xi::Math::atan2(direction.y, horizontalDistance);
  _rotation.y = Xi::Math::atan2(direction.x, direction.z);
  this->touch();
}

GeodeticSphere::GeodeticSphere() {
  // Default to Earth (WGS 84)
  config.a = 6378137.0f;
  config.f = 1.0f / 298.257223563f;
}

GeoPos GeodeticSphere::getGeoPos(const Transform &t) const {
  // Simplified conversion for demonstration.
  // Real logic involves ECEF to LLA transformation.
  // Assuming position() is in meters relative to sphere center.
  Vector3 pos = t.getPosition();
  f32 x = pos.x;
  f32 y = pos.y;
  f32 z = pos.z;

  f32 r = Xi::Math::sqrt(x * x + y * y + z * z);
  if (r < 1e-6f)
    return {0, 0, 0};

  f32 lat = Xi::Math::asin(z / r) * (180.0f / 3.14159265f);
  f32 lng = Xi::Math::atan2(y, x) * (180.0f / 3.14159265f);
  f32 alt = r - config.a;

  return {lat, lng, alt};
}

} // namespace Xi
