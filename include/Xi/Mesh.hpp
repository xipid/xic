#ifndef XI_MESH
#define XI_MESH

#include "Graphics.hpp"

#include "Spatial.hpp"

namespace Xi {
#pragma pack(push, 1)
struct XI_EXPORT Vertex {
  f32 x, y, z;
  f32 u, v;
  f32 nx, ny, nz;
  u32 j[4];
  f32 w[4];
};
#pragma pack(pop)

struct XI_EXPORT Mesh3 {
  Array<Vertex> vertices;
  Array<u32> indices;

  void *_vb = nullptr;
  void *_ib = nullptr;
  bool dirty = true;

  void upload() {
    // We only upload if the mesh is dirty and has data
    if (!dirty || vertices.length() == 0)
      return;

    // Clean up old GPU resources before creating new ones
    GraphicsContext::release(_vb);
    GraphicsContext::release(_ib);

    // Since our Array<Vertex> is already packed correctly,
    // we can pass the pointer directly to the GPU buffer.
    // This is much faster than the old manual loop!
    gContext.createBuffer(vertices.data(),
                          (u32)(vertices.length() * sizeof(Vertex)), false,
                          &_vb);

    // Upload indices if they exist
    if (indices.length() > 0) {
      gContext.createBuffer(indices.data(),
                            (u32)(indices.length() * sizeof(u32)), true, &_ib);
    }

    dirty = false;
  }

  ~Mesh3() {
    GraphicsContext::release(_vb);
    GraphicsContext::release(_ib);
  }
};

} // namespace Xi
#endif