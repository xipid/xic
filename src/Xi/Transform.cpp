/**
 * @file Transform.cpp
 * @brief Implementation of spatial transformations and geodetic coordinate
 * systems.
 */

#include "../../include/Xi/Transform.hpp"
#include "../../include/Xi/Math.hpp"

namespace Xi {

Matrix4 Transform::getMatrix() const {
  if (transformVersion != _cachedVersion) {
    _cachedMatrix =
        multiply(multiply(rotateX(_rotation.x), rotateY(_rotation.y)),
                 translate(_position.x, _position.y, _position.z));
    _cachedVersion = transformVersion;
  }
  return _cachedMatrix;
}

void Transform::lookAt(Vector3 target, Vector3 up) {
  Vector3 direction = {target.x - _position.x, target.y - _position.y,
                       target.z - _position.z};
  float horizontalDistance =
      sqrt(direction.x * direction.x + direction.z * direction.z);
  _rotation.x = -atan2(direction.y, horizontalDistance);
  _rotation.y = atan2(direction.x, direction.z);
  this->touch();
}

// Geodesy implementation moved to header for performance (inline),
// but we keep the file for potential future expansion.

} // namespace Xi
