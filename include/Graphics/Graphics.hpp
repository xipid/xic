/**
 * @file Graphics.hpp
 * @brief Core graphics context and rendering engine integration for the Xi
 * framework.

 */

#ifndef XI_GRAPHICS_GRAPHICS_HPP
#define XI_GRAPHICS_GRAPHICS_HPP

#include "../Xi/Device.hpp"

// CRITICAL: Undefine Linux system macros that collide with Diligent
#ifdef MAP_TYPE
#undef MAP_TYPE
#endif
#ifdef MAP_WRITE
#undef MAP_WRITE
#endif
#ifdef MAP_READ
#undef MAP_READ
#endif

#define PLATFORM_LINUX 1

// 1. First, define the platform and basic types
// #include "Graphics/GraphicsEngine/interface/LoadEngineDll.h" // Essential for
// 'LoadEngineDll'
#include "Platforms/interface/NativeWindow.h" // Essential for 'NativeWindow' type
#include "Primitives/interface/BasicTypes.h"

// 2. Common Diligent structures
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Platforms/Basic/interface/DebugUtilities.hpp"

// 3. General Graphics Interfaces
#include "Graphics/GraphicsEngine/interface/CommandList.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"

// 4. Finally, the Vulkan specific factory
// #include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"

/**
 * @namespace Graphics
 * @brief Contains rendering, windowing, and graphics primitives.
 */
namespace Graphics {

using namespace Xi;
using namespace Collection;

/**
 * @struct GraphicsContext
 * @brief Global singleton for the GPU device and immediate context.
 */
struct XI_EXPORT GraphicsContext {
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice>
      device; ///< The hardware rendering device.
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext>
      ctx; ///< Immediate device context for commands.
  Array<Diligent::RefCntAutoPtr<Diligent::IDeviceContext>>
      deferred; ///< Deferred contexts for multi-threaded command recording.
  Diligent::RefCntAutoPtr<Diligent::IPipelineState>
      blitPSO; ///< Pipeline state for blitting textures.

  /**
   * @brief Initializes the global graphics context.
   */
  void init();

  /**
   * @brief Sets the current pipeline state.
   * @param pso Pointer to the IPipelineState.
   */
  void setPipelineState(void *pso);

  /**
   * @brief Commits shader resource bindings.
   * @param srb Pointer to the IShaderResourceBinding.
   */
  void commitResources(void *srb);

  /**
   * @brief Binds render targets and depth buffer.
   * @param rtv Render target view.
   * @param dsv Depth stencil view.
   * @param w Viewport width.
   * @param h Viewport height.
   */
  void bindResources(void *rtv, void *dsv, int w, int h);

  /**
   * @brief Draws a mesh using vertex and index buffers.
   * @param vb Vertex buffer pointer.
   * @param ib Index buffer pointer.
   * @param indices Number of indices to draw.
   */
  void drawMesh(void *vb, void *ib, u32 indices);

  /**
   * @brief Creates a GPU buffer.
   * @param data Initial data pointer.
   * @param size Size in bytes.
   * @param isIndex Whether this is an index buffer.
   * @param buf Pointer to the created buffer handle.
   */
  void createBuffer(void *data, u32 size, bool isIndex, void **buf);

  /**
   * @brief Maps a GPU buffer to CPU memory.
   * @param buffer The buffer handle.
   * @return Pointer to the mapped memory.
   */
  void *mapBuffer(void *buffer);

  /**
   * @brief Unmaps a GPU buffer.
   * @param buffer The buffer handle.
   */
  void unmapBuffer(void *buffer);

  /**
   * @brief Releases a GPU resource.
   * @param res The resource handle.
   */
  static void release(void *res);
};

/**
 * @var gContext
 * @brief Global graphics context instance.
 */
extern XI_EXPORT GraphicsContext gContext;

/**
 * @struct SwapContext
 * @brief Manages the swap chain and presentation context for a window.
 */
struct XI_EXPORT SwapContext {
  Diligent::RefCntAutoPtr<Diligent::ISwapChain> chain; ///< Diligent swap chain.
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>
      blitSRB;           ///< SRB for fullscreen blit.
  void *_win = nullptr;  ///< Native window handle.
  void *_disp = nullptr; ///< Native display handle.

  void setWin(void *w) { _win = w; }
  void setDisp(void *d) { _disp = d; }

  /**
   * @brief Initializes the swap context for the current window.
   */
  void init();

  /**
   * @brief Presents the back buffer to the screen.
   */
  void present();

  /**
   * @brief Resizes the swap chain buffers.
   * @param w New width.
   * @param h New height.
   */
  void resize(int w, int h);

  /**
   * @brief Gets the current render target view (RTV).
   * @return Pointer to ITextureView.
   */
  void *getRTV();

  /**
   * @brief Gets the current depth stencil view (DSV).
   * @return Pointer to ITextureView.
   */
  void *getDSV();

  /**
   * @brief Performs a fullscreen blit of a shader resource view (SRV).
   * @param srv The source SRV to blit.
   */
  void drawFullscreen(void *srv);
};

/**
 * @class GLFWDiligentRenderingDevice
 * @brief A rendering device implementation that manages GPU memory and
 * resources.
 */
class XI_EXPORT GLFWDiligentRenderingDevice : public DeviceRenderingDevice {
public:
  GLFWDiligentRenderingDevice();
  virtual ~GLFWDiligentRenderingDevice() = default;

  void *alloc(usz size) override;
  void *allocSurface(i32 w, i32 h, i32 channels = 4) override;
  void free(void *handle) override;
  void upload(void *handle, const void *src, usz size) override;
  void download(void *handle, void *dst, usz size) override;
  void *view(void *handle, i32 type = 0) override;
};

} // namespace Graphics

#endif // XI_GRAPHICS_GRAPHICS_HPP
