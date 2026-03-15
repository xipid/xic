#include "../../include/Xi/Graphics.hpp"
#include <vector>

namespace Xi {

GraphicsContext gContext;

void GraphicsContext::init() {
  if (device)
    return;

  Diligent::EngineVkCreateInfo CI;
  auto *F = Diligent::GetEngineFactoryVk();

  // Disable Diligent logging by setting an empty callback on the factory
  F->SetMessageCallback([](Diligent::DEBUG_MESSAGE_SEVERITY, const char *,
                           const char *, const char *, int) {});

  CI.NumDeferredContexts = 1;

  std::vector<Diligent::IDeviceContext *> ppContexts(1 +
                                                     CI.NumDeferredContexts);
  F->CreateDeviceAndContextsVk(CI, &device, ppContexts.data());

  ctx = ppContexts[0];
  for (u32 i = 1; i < (u32)ppContexts.size(); ++i)
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

void GraphicsContext::setPipelineState(void *pso) {
  ctx->SetPipelineState((Diligent::IPipelineState *)pso);
}

void GraphicsContext::commitResources(void *srb) {
  ctx->CommitShaderResources(
      (Diligent::IShaderResourceBinding *)srb,
      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void GraphicsContext::bindResources(void *rtv, void *dsv, int w, int h) {
  float Clr[] = {0.1f, 0.1f, 0.3f, 1.0f};

  Diligent::ITextureView *pRTVs[] = {(Diligent::ITextureView *)rtv};
  ctx->SetRenderTargets(1, pRTVs, (Diligent::ITextureView *)dsv,
                        Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::Viewport VP;
  VP.Width = (float)w;
  VP.Height = (float)h;
  VP.MinDepth = 0.0f;
  VP.MaxDepth = 1.0f;
  ctx->SetViewports(1, &VP, 0, 0);

  ctx->ClearRenderTarget((Diligent::ITextureView *)rtv, Clr,
                         Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  if (dsv)
    ctx->ClearDepthStencil((Diligent::ITextureView *)dsv,
                           Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  Diligent::Viewport V;
  V.Width = (float)w;
  V.Height = (float)h;
  V.MaxDepth = 1.0f;
  ctx->SetViewports(1, &V, w, h);

  Diligent::Rect S = {0, 0, w, h};
  ctx->SetScissorRects(1, &S, w, h);
}

void GraphicsContext::drawMesh(void *vb, void *ib, u32 indices) {
  if (vb == nullptr || ib == nullptr || indices == 0) {
    return;
  }

  Diligent::Uint64 offset = 0;
  Diligent::IBuffer *pVBs[] = {(Diligent::IBuffer *)vb};

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

void GraphicsContext::createBuffer(void *data, u32 size, bool isIndex,
                                   void **buf) {
  if (!device)
    return;
  Diligent::BufferDesc D;
  D.BindFlags =
      isIndex ? Diligent::BIND_INDEX_BUFFER : Diligent::BIND_VERTEX_BUFFER;
  D.Size = size;
  D.Usage = Diligent::USAGE_IMMUTABLE;
  Diligent::BufferData Init = {data, size};
  device->CreateBuffer(D, &Init, (Diligent::IBuffer **)buf);
}

void *GraphicsContext::mapBuffer(void *buffer) {
  if (!buffer || !ctx)
    return nullptr;
  void *pData = nullptr;
  ctx->MapBuffer((Diligent::IBuffer *)buffer, Diligent::MAP_WRITE,
                 Diligent::MAP_FLAG_DISCARD, pData);
  return pData;
}

void GraphicsContext::unmapBuffer(void *buffer) {
  if (!buffer || !ctx)
    return;
  ctx->UnmapBuffer((Diligent::IBuffer *)buffer, Diligent::MAP_WRITE);
}

void GraphicsContext::release(void *res) {
  if (res)
    ((Diligent::IDeviceObject *)res)->Release();
}

void SwapContext::init() {
  if (chain || !_win)
    return;
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

void SwapContext::present() {
  if (chain)
    chain->Present(1);
}

void SwapContext::resize(int w, int h) {
  if (chain)
    chain->Resize(w, h);
}

void *SwapContext::getRTV() {
  init();
  return chain->GetCurrentBackBufferRTV();
}
void *SwapContext::getDSV() {
  init();
  return chain->GetDepthBufferDSV();
}

void SwapContext::drawFullscreen(void *srv) {
  if (!srv)
    return;
  auto *pRTV = chain->GetCurrentBackBufferRTV();
  auto *pDSV = chain->GetDepthBufferDSV();
  gContext.ctx->SetRenderTargets(
      1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  auto &SCDesc = chain->GetDesc();
  Diligent::Viewport VP;
  VP.Width = (float)SCDesc.Width;
  VP.Height = (float)SCDesc.Height;
  VP.MinDepth = 0.0f;
  VP.MaxDepth = 1.0f;
  gContext.ctx->SetViewports(1, &VP, 0, 0);

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

// GpuSurface implementation details (if any needed in cpp)
struct GpuSurface {
  Diligent::RefCntAutoPtr<Diligent::ITexture> tex;
  i32 w = 0, h = 0;
};

GLFWDiligentRenderingDevice::GLFWDiligentRenderingDevice() {
  name = "DiligentVk";
  outputIntended = false;
  gContext.init();
  device = gContext.device.RawPtr();
}

void *GLFWDiligentRenderingDevice::alloc(usz size) {
  auto *gs = new GpuSurface();
  return gs;
}

void *GLFWDiligentRenderingDevice::allocSurface(i32 w, i32 h, i32 channels) {
  auto *gs = new GpuSurface();
  gs->w = w;
  gs->h = h;

  Diligent::TextureDesc desc;
  desc.Name = "Xi_DeviceSurface";
  desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
  desc.Width = (uint32_t)w;
  desc.Height = (uint32_t)h;
  desc.Format = Diligent::TEX_FORMAT_BGRA8_UNORM;
  desc.BindFlags =
      Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET;
  desc.Usage = Diligent::USAGE_DEFAULT;

  gContext.device->CreateTexture(desc, nullptr, &gs->tex);
  return gs;
}

void GLFWDiligentRenderingDevice::free(void *handle) {
  delete (GpuSurface *)handle;
}

void GLFWDiligentRenderingDevice::upload(void *handle, const void *src,
                                         usz size) {
  auto *gs = (GpuSurface *)handle;
  if (!gs || !gs->tex || !src)
    return;

  Diligent::TextureSubResData subRes;
  subRes.pData = src;
  subRes.Stride = (uint64_t)gs->w * 4;

  Diligent::Box box;
  box.MinX = 0;
  box.MinY = 0;
  box.MinZ = 0;
  box.MaxX = (uint32_t)gs->w;
  box.MaxY = (uint32_t)gs->h;
  box.MaxZ = 1;

  gContext.ctx->UpdateTexture(
      gs->tex, 0, 0, box, subRes,
      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void GLFWDiligentRenderingDevice::download(void *handle, void *dst, usz size) {
  auto *gs = (GpuSurface *)handle;
  if (!gs || !gs->tex || !dst)
    return;

  Diligent::TextureDesc stagingDesc = gs->tex->GetDesc();
  stagingDesc.Usage = Diligent::USAGE_STAGING;
  stagingDesc.BindFlags = Diligent::BIND_NONE;
  stagingDesc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;

  Diligent::RefCntAutoPtr<Diligent::ITexture> pStaging;
  gContext.device->CreateTexture(stagingDesc, nullptr, &pStaging);

  Diligent::CopyTextureAttribs copyAttribs(
      gs->tex, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, pStaging,
      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  gContext.ctx->CopyTexture(copyAttribs);
  gContext.ctx->WaitForIdle();

  Diligent::MappedTextureSubresource mapped;
  gContext.ctx->MapTextureSubresource(pStaging, 0, 0, Diligent::MAP_READ,
                                      Diligent::MAP_FLAG_NONE, nullptr, mapped);

  usz rowBytes = (usz)gs->w * 4;
  for (i32 row = 0; row < gs->h; ++row) {
    memcpy((u8 *)dst + row * rowBytes,
           (const u8 *)mapped.pData + row * mapped.Stride, rowBytes);
  }
  gContext.ctx->UnmapTextureSubresource(pStaging, 0, 0);
}

void *GLFWDiligentRenderingDevice::view(void *handle, i32 type) {
  auto *gs = (GpuSurface *)handle;
  if (!gs || !gs->tex)
    return nullptr;
  switch (type) {
  case 0:
    return gs->tex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
  case 1:
    return gs->tex->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
  case 2:
    return gs->tex->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
  default:
    return nullptr;
  }
}

} // namespace Xi
