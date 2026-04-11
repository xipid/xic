/**
 * @file Mesh.hpp
 * @brief Vertex and mesh structures for 3D rendering in the Xi Graphics module.

 */

#ifndef XI_GRAPHICS_MESH_HPP
#define XI_GRAPHICS_MESH_HPP

#include <Graphics/Graphics.hpp>

/**
 * @namespace Graphics
 * @brief Contains rendering, windowing, and graphics primitives.
 */
namespace Graphics {

using namespace Xi;

#pragma pack(push, 1)
/**
 * @struct Vertex
 * @brief Represents a single vertex in a 3D mesh.
 */
struct XI_EXPORT Vertex {
  f32 x, y, z;    ///< Position coordinates.
  f32 u, v;       ///< Texture coordinates (UV).
  f32 nx, ny, nz; ///< Normal vector components.
  u32 j[4];       ///< Joint indices for skeletal animation.
  f32 w[4];       ///< Joint weights for skeletal animation.
};
#pragma pack(pop)

/**
 * @struct Mesh3
 * @brief A collection of vertices and indices defining a 3D object.
 */
struct XI_EXPORT Mesh3 {
  Array<Vertex> vertices; ///< List of vertices.
  Array<u32> indices;     ///< List of vertex indices.

  void *_vb = nullptr; ///< Internal vertex buffer handle.
  void *_ib = nullptr; ///< Internal index buffer handle.
  bool dirty =
      true; ///< Flag indicating if the mesh needs to be re-uploaded to the GPU.

  /**
   * @brief Uploads the mesh data to the GPU.
   */
  void upload() {
    if (!dirty || vertices.length() == 0)
      return;

    if (_vb) {
      gContext.release(_vb);
      _vb = nullptr;
    }
    if (_ib) {
      gContext.release(_ib);
      _ib = nullptr;
    }

    gContext.createBuffer(vertices.data(),
                          (u32)(vertices.length() * sizeof(Vertex)), false,
                          &_vb);

    if (indices.length() > 0) {
      gContext.createBuffer(indices.data(),
                            (u32)(indices.length() * sizeof(u32)), true, &_ib);
    }

    dirty = false;
  }

  /**
   * @brief Destructor ensures GPU resources are released.
   */
  ~Mesh3() {
    gContext.release(_vb);
    gContext.release(_ib);
  }
};

} // namespace Graphics

#endif // XI_GRAPHICS_MESH_HPP
