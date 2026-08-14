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
        multiply(multiply(multiply(rotateX(rotation.x), rotateY(rotation.y)), rotateZ(rotation.z)),
                 translate(position.x, position.y, position.z));
    _cachedVersion = transformVersion;
  }
  return _cachedMatrix;
}

void Transform::lookAt(Vector3 target, Vector3 up) {
  Vector3 direction = {target.x - position.x, target.y - position.y,
                       target.z - position.z};
  float horizontalDistance =
      sqrt(direction.x * direction.x + direction.y * direction.y);
  rotation.x = -atan2(direction.z, horizontalDistance);
  rotation.y = atan2(direction.x, direction.y);
  this->update();
}

// Geodesy implementation moved to header for performance (inline),
// but we keep the file for potential future expansion.

} // namespace Xi
