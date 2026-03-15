#ifndef XI_GRAPHICS_HPP
#define XI_GRAPHICS_HPP

#include "Math.hpp"
#include "Device.hpp"
#include <vector>

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

// Diligent
#include "CommandList.h"
#include "DeviceContext.h"
#include "EngineFactoryVk.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "SwapChain.h"

namespace Xi {

// 1. The Global Singleton for the GPU Device
struct GraphicsContext {
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> ctx; // Immediate Context
  std::vector<Diligent::RefCntAutoPtr<Diligent::IDeviceContext>> deferred;
  Diligent::RefCntAutoPtr<Diligent::IPipelineState> blitPSO;

  void init();

  // Action methods for maximum speed
  void setPipelineState(void *pso);
  void commitResources(void *srb);
  void bindResources(void *rtv, void *dsv, int w, int h);
  void drawMesh(void *vb, void *ib, u32 indices);
  void createBuffer(void *data, u32 size, bool isIndex, void **buf);
  void *mapBuffer(void *buffer);
  void unmapBuffer(void *buffer);
  static void release(void *res);
};

extern GraphicsContext gContext;

// 2. The Window-Specific Context
struct SwapContext {
  Diligent::RefCntAutoPtr<Diligent::ISwapChain> chain;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> blitSRB;
  void *_win = nullptr;
  void *_disp = nullptr;

  void setWin(void *w) { _win = w; }
  void setDisp(void *d) { _disp = d; }

  void init();
  void present();
  void resize(int w, int h);
  void *getRTV();
  void *getDSV();
  void drawFullscreen(void *srv);
};

// -------------------------------------------------------------------------
// GLFWDiligentRenderingDevice — Implementation for GPU
// -------------------------------------------------------------------------
class GLFWDiligentRenderingDevice : public DeviceRenderingDevice {
public:
  GLFWDiligentRenderingDevice();
  void *alloc(usz size) override;
  void *allocSurface(i32 w, i32 h, i32 channels = 4) override;
  void free(void *handle) override;
  void upload(void *handle, const void *src, usz size) override;
  void download(void *handle, void *dst, usz size) override;
  void *view(void *handle, i32 type = 0) override;
};

} // namespace Xi

#endif