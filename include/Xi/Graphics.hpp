#ifndef XI_GRAPHICS
#define XI_GRAPHICS

#include "Math.hpp"

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

  void init() {
    if (device)
      return;

    Diligent::EngineVkCreateInfo CI;
    CI.NumDeferredContexts = 1;
    auto *F = Diligent::GetEngineFactoryVk();

    std::vector<Diligent::IDeviceContext *> ppContexts(1 +
                                                       CI.NumDeferredContexts);
    F->CreateDeviceAndContextsVk(CI, &device, ppContexts.data());

    ctx = ppContexts[0];
    for (u32 i = 1; i < ppContexts.size(); ++i)
      deferred.push_back(
          Diligent::RefCntAutoPtr<Diligent::IDeviceContext>(ppContexts[i]));

    Diligent::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Xi_Blit_PSO";
    PSOCreateInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] =
        Diligent::TEX_FORMAT_BGRA8_UNORM;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology =
        Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode =
        Diligent::CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;

    const char *BlitSource = R"(
            Texture2D    g_Texture;
            SamplerState g_Texture_sampler;
            struct PSInput { float4 Pos : SV_POSITION; float2 UV : TEX_COORD; };
            void VSMain(in uint id : SV_VertexID, out PSInput PSOut) {
                PSOut.UV = float2((id << 1) & 2, id & 2);
                PSOut.Pos = float4(PSOut.UV * 2.0 - 1.0, 0.0, 1.0);
                PSOut.UV.y = 1.0 - PSOut.UV.y;
            }
            void PSMain(in PSInput PSIn, out float4 Color : SV_TARGET) {
                Color = g_Texture.Sample(g_Texture_sampler, PSIn.UV);
            }
        )";

    Diligent::ShaderCreateInfo ShaderCI;
    ShaderCI.Source = BlitSource;
    ShaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;

    Diligent::RefCntAutoPtr<Diligent::IShader> pVS, pPS;
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    ShaderCI.EntryPoint = "VSMain";
    device->CreateShader(ShaderCI, &pVS);
    ShaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    ShaderCI.EntryPoint = "PSMain";
    device->CreateShader(ShaderCI, &pPS);

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    Diligent::ShaderResourceVariableDesc Vars[] = {
        {Diligent::SHADER_TYPE_PIXEL, "g_Texture",
         Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
    PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = 1;

    Diligent::ImmutableSamplerDesc ImtblSamplers[] = {
        {Diligent::SHADER_TYPE_PIXEL, "g_Texture_sampler",
         Diligent::SamplerDesc{}}};
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = ImtblSamplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = 1;

    device->CreateGraphicsPipelineState(PSOCreateInfo, &blitPSO);
  }

  // Action methods for maximum speed
  inline void setPipelineState(void *pso) {
    ctx->SetPipelineState((Diligent::IPipelineState *)pso);
  }

  inline void commitResources(void *srb) {
    ctx->CommitShaderResources(
        (Diligent::IShaderResourceBinding *)srb,
        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  }

  // inline void drawMesh(void *vb, void *ib, u32 indices)
  // {
  //     if (!vb || !ib)
  //         return;
  //     Diligent::Uint64 offset = 0;
  //     Diligent::IBuffer *pVBs[] = {(Diligent::IBuffer *)vb};
  //     ctx->SetVertexBuffers(0, 1, pVBs, &offset,
  //     Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
  //     Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
  //     ctx->SetIndexBuffer((Diligent::IBuffer *)ib, 0,
  //     Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  //     Diligent::DrawIndexedAttribs DrawAttrs;
  //     DrawAttrs.NumIndices = indices;
  //     DrawAttrs.IndexType = Diligent::VT_UINT32;
  //     ctx->DrawIndexed(DrawAttrs);
  // }

  inline void bindResources(void *rtv, void *dsv, int w, int h) {
    float Clr[] = {0.1f, 0.1f, 0.3f, 1.0f};
    // Changed gContext.pContext to pImmediateCtx
    ctx->SetRenderTargets(1, (Diligent::ITextureView **)&rtv,
                          (Diligent::ITextureView *)dsv,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ctx->ClearRenderTarget((Diligent::ITextureView *)rtv, Clr,
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if (dsv)
      ctx->ClearDepthStencil(
          (Diligent::ITextureView *)dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::Viewport V;
    V.Width = (float)w;
    V.Height = (float)h;
    V.MaxDepth = 1.0f;
    ctx->SetViewports(1, &V, w, h);

    Diligent::Rect S = {0, 0, w, h};
    ctx->SetScissorRects(1, &S, w, h);
  }

  void drawMesh(void *vb, void *ib, u32 indices) {
    // Safety check to prevent Segmentation Fault
    if (vb == nullptr || ib == nullptr || indices == 0) {
      return;
    }

    Diligent::Uint64 offset = 0;
    Diligent::IBuffer *pVBs[] = {(Diligent::IBuffer *)vb};

    // This is the line that satisfies the "1 input buffer slots" requirement
    ctx->SetVertexBuffers(0, 1, pVBs, &offset,
                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                          Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

    ctx->SetIndexBuffer((Diligent::IBuffer *)ib, 0,
                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawIndexedAttribs DrawAttrs;
    DrawAttrs.NumIndices = indices;
    DrawAttrs.IndexType = Diligent::VT_UINT32;
    DrawAttrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;

    ctx->DrawIndexed(DrawAttrs);
  }

  void createBuffer(void *data, uint32_t size, bool isIndex, void **buf) {
    Diligent::BufferDesc D;
    D.BindFlags =
        isIndex ? Diligent::BIND_INDEX_BUFFER : Diligent::BIND_VERTEX_BUFFER;
    D.Size = size;
    D.Usage = Diligent::USAGE_IMMUTABLE;
    Diligent::BufferData Init = {data, size};
    device->CreateBuffer(D, &Init, (Diligent::IBuffer **)buf);
  }

  void *mapBuffer(void *buffer) {
    void *pData = nullptr;
    // Use the explicit MapType enum to avoid macro replacement issues
    ctx->MapBuffer((Diligent::IBuffer *)buffer, Diligent::MAP_WRITE,
                   Diligent::MAP_FLAG_DISCARD, pData);
    return pData;
  }

  void unmapBuffer(void *buffer) {
    ctx->UnmapBuffer((Diligent::IBuffer *)buffer, Diligent::MAP_WRITE);
  }

  static void release(void *res) {
    if (res)
      ((Diligent::IDeviceObject *)res)->Release();
  }

  // void setPipelineState(void *pso) {
  // ctx->SetPipelineState((Diligent::IPipelineState *)pso); }
};

// Singleton instance
static GraphicsContext gContext;

// 2. The Window-Specific Context
struct SwapContext {
  Diligent::RefCntAutoPtr<Diligent::ISwapChain> chain;
  Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> blitSRB;
  void *_win = nullptr;
  void *_disp = nullptr;

  void setWin(void *w) { _win = w; }
  void setDisp(void *d) { _disp = d; }

  void init() {
    if (chain || !_win)
      return;
    // Ensure the global device exists first
    gContext.init();

    Diligent::SwapChainDesc SC;
    SC.ColorBufferFormat = Diligent::TEX_FORMAT_BGRA8_UNORM;
    SC.DepthBufferFormat = Diligent::TEX_FORMAT_D32_FLOAT;

    Diligent::LinuxNativeWindow LW;
    LW.WindowId = (Diligent::Uint32)(size_t)_win;
    LW.pDisplay = _disp;

    auto *F = Diligent::GetEngineFactoryVk();
    F->CreateSwapChainVk(gContext.device, gContext.ctx, SC, LW, &chain);

    gContext.blitPSO->CreateShaderResourceBinding(&blitSRB, true);
  }

  void present() {
    if (chain)
      chain->Present();
  }

  inline void resize(int w, int h) {
    if (chain)
      chain->Resize(w, h);
  }

  void *getRTV() {
    init();
    return chain->GetCurrentBackBufferRTV();
  }
  void *getDSV() {
    init();
    return chain->GetDepthBufferDSV();
  }

  void drawFullscreen(void *srv) {
    auto *pRTV = chain->GetCurrentBackBufferRTV();
    auto *pDSV = chain->GetDepthBufferDSV();
    gContext.ctx->SetRenderTargets(
        1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    auto *pVar =
        blitSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
    if (pVar)
      pVar->Set((Diligent::ITextureView *)srv);

    gContext.ctx->SetPipelineState(gContext.blitPSO);
    gContext.ctx->CommitShaderResources(
        blitSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::DrawAttribs drawAttrs;
    drawAttrs.NumVertices = 3;
    gContext.ctx->Draw(drawAttrs);
  }
};
} // namespace Xi

#endif