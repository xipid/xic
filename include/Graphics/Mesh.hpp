/**
 * @file Mesh.hpp
 * @brief Vertex and mesh structures for 3D rendering in the Xi Graphics module.

 */

#ifndef XI_GRAPHICS_MESH_HPP
#define XI_GRAPHICS_MESH_HPP

#include <Graphics/Graphics.hpp>
#include <XiN/Reflect.hpp>

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

/**
 * @struct MeshVertAcc
 * @brief Property accessor for mesh vertices (for reflection/scripts).
 */
struct MeshVertAcc : public PropertyAccessor {
  MeshVertAcc() { name = "vertices"; }
  void *getPtr(void *obj) override { return &(((Mesh3 *)obj)->vertices); }
  void pushFloatArray(void *obj, float *vals, usz count) override {
    auto &arr = ((Mesh3 *)obj)->vertices;
    usz numVerts = count / 16;
    for (usz i = 0; i < numVerts; ++i) {
      Vertex v;
      float *b = vals + i * 16;
      v.x = b[0];
      v.y = b[1];
      v.z = b[2];
      v.u = b[3];
      v.v = b[4];
      v.nx = b[5];
      v.ny = b[6];
      v.nz = b[7];
      v.j[0] = (u32)b[8];
      v.j[1] = (u32)b[9];
      v.j[2] = (u32)b[10];
      v.j[3] = (u32)b[11];
      v.w[0] = b[12];
      v.w[1] = b[13];
      v.w[2] = b[14];
      v.w[3] = b[15];
      arr.push(v);
    }
    ((Mesh3 *)obj)->dirty = true;
  }
};

/**
 * @struct MeshIndAcc
 * @brief Property accessor for mesh indices (for reflection/scripts).
 */
struct MeshIndAcc : public PropertyAccessor {
  MeshIndAcc() { name = "indices"; }
  void *getPtr(void *obj) override { return &(((Mesh3 *)obj)->indices); }
  void pushUIntArray(void *obj, u32 *vals, usz count) override {
    auto &arr = ((Mesh3 *)obj)->indices;
    for (usz i = 0; i < count; ++i)
      arr.push(vals[i]);
    ((Mesh3 *)obj)->dirty = true;
  }
};

} // namespace Graphics

/**
 * Reflection setup for Mesh3.
 */
template <> struct Reflect<Graphics::Mesh3> {
  static void setup(TypeMeta *meta) {
    meta->properties.push(new Graphics::MeshVertAcc());
    meta->properties.push(new Graphics::MeshIndAcc());
  }
};

#endif // XI_GRAPHICS_MESH_HPP

#endif