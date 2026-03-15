#ifndef XI_CAMERA_HPP
#define XI_CAMERA_HPP

#include "Func.hpp"
#include "Graphics.hpp"
#include "Mesh.hpp"
#include "Shader.hpp"
#include "Tree.hpp"

namespace Xi {
struct XI_EXPORT Renderable3 : public TreeItem, public Transform {
  Mesh3 *mesh = nullptr;
  Shader *shader = nullptr;

  // Surface: raw pixel data (InlineArray<u8> aka String)
  // Can live on CPU or GPU via .to(device)
  String *surface = nullptr;

  // GPU handle cache for rendering (managed by the rendering device)
  Diligent::RefCntAutoPtr<Diligent::ITexture> gpuTexture;
};

struct XI_EXPORT ShaderData {
  Matrix4 mvp;   // 64 bytes
  Matrix4 world; // 64 bytes
};

struct XI_EXPORT Camera3 : public Transform {
  TreeItem *root = nullptr;

  // Output surface (pixel data). Now device-aware.
  String surface;
  i32 surfaceWidth = 0;
  i32 surfaceHeight = 0;

  // The device that will provide the memory for the surface (e.g. GPU)
  IMemoryDevice *device = nullptr;

  float clipStart = 0.1f;
  float clipEnd = 100.0f;

  float shiftX = 0.0f;
  float shiftY = 0.0f;

  bool isOrtho = false;
  float fov = 50.0f;
  float orthoScale = 8.0f;

  // Depth buffer - still managed internally for rendering
  Diligent::RefCntAutoPtr<Diligent::ITextureView> pDSV;

  Func<void()> onUpdate;

  Camera3() = default;
  virtual ~Camera3() = default;

  /// Get the shader resource view for displaying this camera's output.
  /// Extracts it directly from the surface view.
  void *getView();

  /// Ensure GPU render target exists and matches current dimensions
  void touchGPU();

  void render(void *rtv, void *dsv, i32 w, i32 h);
  void render();

private:
  void _ensureDepthBuffer(i32 w, i32 h);
  static void _renderRec(TreeItem *n, Matrix4 p, const Matrix4 &vp);
};
} // namespace Xi

#endif