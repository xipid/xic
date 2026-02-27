#ifndef XI_MESH
#define XI_MESH

#include "Graphics.hpp"

namespace Xi {
#pragma pack(push, 1)
struct Vertex {
  f32 x, y, z;
  f32 u, v;
  f32 nx, ny, nz;
  u32 j[4];
  f32 w[4];
};
#pragma pack(pop)

struct Mesh3 {
  Array<Vertex> vertices;
  Array<u32> indices;

  void *_vb = nullptr;
  void *_ib = nullptr;
  bool dirty = true;

  void upload() {
    // We only upload if the mesh is dirty and has data
    if (!dirty || vertices.length == 0)
      return;

    // Clean up old GPU resources before creating new ones
    GraphicsContext::release(_vb);
    GraphicsContext::release(_ib);

    // Since our Array<Vertex> is already packed correctly,
    // we can pass the pointer directly to the GPU buffer.
    // This is much faster than the old manual loop!
    gContext.createBuffer(vertices.data(),
                          (u32)(vertices.length * sizeof(Vertex)), false, &_vb);

    // Upload indices if they exist
    if (indices.length > 0) {
      gContext.createBuffer(indices.data(), (u32)(indices.length * sizeof(u32)),
                            true, &_ib);
    }

    dirty = false;
  }

  ~Mesh3() {
    GraphicsContext::release(_vb);
    GraphicsContext::release(_ib);
  }
};

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