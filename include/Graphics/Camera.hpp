/**
 * @file Camera.hpp
 * @brief Camera and renderable object definitions for the Xi Graphics module.

 */

#ifndef XI_GRAPHICS_CAMERA_HPP
#define XI_GRAPHICS_CAMERA_HPP

#include "../Collection/Tree.hpp"
#include "../Xi/Func.hpp"
#include "../Xi/Transform.hpp"
#include "Graphics.hpp"
#include "Mesh.hpp"
#include "Shader.hpp"

/**
 * @namespace Graphics
 * @brief Contains rendering, windowing, and graphics primitives.
 */
namespace Graphics {

using namespace Xi;

/**
 * @struct Renderable3
 * @brief Represents a 3D object that can be rendered.
 */
struct XI_EXPORT Renderable3 : public TreeItem, public Transform {
  Mesh3 *mesh = nullptr;    ///< Pointer to the mesh data.
  Shader *shader = nullptr; ///< Pointer to the shader used for rendering.

  String *surface = nullptr; ///< Optional CPU/GPU surface (pixel data).

  // GPU handle cache for rendering (managed by the rendering device)
  Diligent::RefCntAutoPtr<Diligent::ITexture> gpuTexture;
};

/**
 * @struct ShaderData
 * @brief Uniform data passed to shaders (MVP matrices).
 */
struct XI_EXPORT ShaderData {
  Matrix4 mvp;   ///< Model-View-Projection matrix (64 bytes).
  Matrix4 world; ///< World transformation matrix (64 bytes).
};

/**
 * @class Camera3
 * @brief Represents a 3D camera with perspective or orthographic projection.
 */
class XI_EXPORT Camera3 : public Transform {
public:
  TreeItem *root = nullptr; ///< Root of the scene tree to render.

  String surface;        ///< Output surface pixel data.
  i32 surfaceWidth = 0;  ///< Width of the output surface.
  i32 surfaceHeight = 0; ///< Height of the output surface.

  IMemoryDevice *device = nullptr; ///< Memory device providing the surface.

  float clipStart = 0.1f; ///< Near clipping plane.
  float clipEnd = 100.0f; ///< Far clipping plane.

  float shiftX = 0.0f; ///< Camera shift X.
  float shiftY = 0.0f; ///< Camera shift Y.

  bool isOrtho = false;    ///< Whether the camera uses orthographic projection.
  float fov = 50.0f;       ///< Field of view (in degrees) for perspective.
  float orthoScale = 8.0f; ///< Scale factor for orthographic projection.

  // Depth buffer managed internally for rendering
  Diligent::RefCntAutoPtr<Diligent::ITextureView> pDSV;

  Func<void()> onUpdate; ///< Callback invoked after camera updates.

  Camera3() = default;
  virtual ~Camera3() = default;

  /**
   * @brief Gets the shader resource view for displaying the camera output.
   * @return Pointer to the view.
   */
  void *getView();

  /**
   * @brief Ensures GPU render target exists and matches current dimensions.
   */
  void touchGPU();

  /**
   * @brief Renders the scene to specific render targets.
   * @param rtv Render target view.
   * @param dsv Depth stencil view.
   * @param w Target width.
   * @param h Target height.
   */
  void render(void *rtv, void *dsv, i32 w, i32 h);

  /**
   * @brief Renders the scene to the camera's internal surface.
   */
  void render();

private:
  void _ensureDepthBuffer(i32 w, i32 h);
  static void _renderRec(TreeItem *ti, Matrix4 p, const Matrix4 &vp);
};

} // namespace Graphics

#endif