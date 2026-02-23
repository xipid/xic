#ifndef XI_Vector
#define XI_Vector

// --- Standard C++ ---
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>

// --- Xi ---
#include "Array.hpp"
#include "Math.hpp"
#include "String.hpp"

namespace Xi {
// Matrix4 and Vector types are now defined in Math.hpp to avoid circular
// dependencies and provide unified math overloads.

// --- Transform ---
struct Transform3 {
  Vector3 position = {0, 0, 0};
  Vector3 rotation = {0, 0, 0};
  Vector3 scale = {1, 1, 1};
  u32 transformVersion = 1;

  void touch() {
    transformVersion++;
    if (transformVersion == 0)
      transformVersion = 1;
  }
  Matrix4 getMatrix() const {
    // Using free functions from Math namespace
    return Math::rotateX(rotation.x) * Math::rotateY(rotation.y) *
           Math::translate(position.x, position.y, position.z);
  }

  void lookAt(Vector3 target, Vector3 up = {0, 1, 0}) {
    // 1. Calculer le vecteur directionnel
    Vector3 direction = {target.x - position.x, target.y - position.y,
                         target.z - position.z};

    // 2. Calculer la distance horizontale (plan XZ)
    float horizontalDistance =
        Xi::Math::sqrt(direction.x * direction.x + direction.z * direction.z);

    // 3. Calculer les angles d'Euler
    // Pitch (Rotation X) : inclinaison vers le haut ou le bas
    rotation.x = -Xi::Math::atan2(direction.y, horizontalDistance);

    // Yaw (Rotation Y) : orientation gauche ou droite
    // Note : On ajoute PI selon l'orientation par défaut de votre modèle
    rotation.y = Xi::Math::atan2(direction.x, direction.z);

    // 4. Marquer la transformation comme modifiée
    this->touch();
  }
};
} // namespace Xi

#endif