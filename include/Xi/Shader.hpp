#ifndef XI_SHADER
#define XI_SHADER

#include "Graphics.hpp"
#include <cstdio>

namespace Xi {

struct XI_EXPORT Shader {
  String vertexSource;
  String pixelSource;
  void *_pso = nullptr;
  void *_srb = nullptr;
  void *_cb = nullptr;

  void create() {
    if (_pso)
      return;
    printf("Xi: Shader::create() VS length: %d\n", (int)vertexSource.length());
    createShader(vertexSource.c_str(), pixelSource.c_str(), &_pso, &_srb, &_cb);
    if (!_pso) {
      printf("Error: Shader PSO creation FAILED!\n");
    } else {
      printf("Xi: Shader created. PSO: %p, SRB: %p, CB: %p\n", _pso, _srb, _cb);
    }
  }
  void updateUniforms(const void *d, u32 s) {
    void *m = gContext.mapBuffer(_cb);
    if (m) {
      memcpy(m, d, s);
      gContext.unmapBuffer(_cb);
    }
  }
  ~Shader() {
    GraphicsContext::release(_pso);
    GraphicsContext::release(_srb);
    GraphicsContext::release(_cb);
  }

private:
  void createShader(const char *vs, const char *ps, void **pso, void **srb,
                    void **cb) {
    Diligent::ShaderCreateInfo CI;
    CI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    Diligent::RefCntAutoPtr<Diligent::IShader> VS, PS;
    CI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    CI.Desc.Name = "VS";
    CI.Source = vs;
    gContext.device->CreateShader(CI, &VS);
    CI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    CI.Desc.Name = "PS";
    CI.Source = ps;
    gContext.device->CreateShader(CI, &PS);

    Diligent::GraphicsPipelineStateCreateInfo P;
    P.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    P.GraphicsPipeline.RTVFormats[0] = Diligent::TEX_FORMAT_BGRA8_UNORM;
    P.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
    P.GraphicsPipeline.NumRenderTargets = 1;
    P.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
    P.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
    P.GraphicsPipeline.DepthStencilDesc.DepthFunc =
        Diligent::COMPARISON_FUNC_LESS;
    P.GraphicsPipeline.PrimitiveTopology =
        Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    P.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;

    // This array tells Diligent how to map your 64-byte C++ struct to the GPU
    Diligent::LayoutElement LayoutElems[] = {
        // Attribute 0: Position (float3)
        {0, 0, 3, Diligent::VT_FLOAT32, false, 0xFFFFFFFF},
        // Attribute 1: UV (float2)
        {1, 0, 2, Diligent::VT_FLOAT32, false, 0xFFFFFFFF},
        // Attribute 2: Normal (float3)
        {2, 0, 3, Diligent::VT_FLOAT32, false, 0xFFFFFFFF},
        // Attribute 3: Joints (uint4)
        {3, 0, 4, Diligent::VT_UINT32, false, 0xFFFFFFFF},
        // Attribute 4: Weights (float4)
        {4, 0, 4, Diligent::VT_FLOAT32, false, 0xFFFFFFFF}};

    P.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    P.GraphicsPipeline.InputLayout.NumElements = 5;
    P.pVS = VS;
    P.pPS = PS;

    P.PSODesc.ResourceLayout.DefaultVariableType =
        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    gContext.device->CreateGraphicsPipelineState(
        P, (Diligent::IPipelineState **)pso);

    // PATCH: Increased size to 128 to accommodate g_MVP and g_Model
    Diligent::BufferDesc CD;
    CD.Size = 128;
    CD.Usage = Diligent::USAGE_DYNAMIC;
    CD.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CD.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    gContext.device->CreateBuffer(CD, nullptr, (Diligent::IBuffer **)cb);

    ((Diligent::IPipelineState *)*pso)
        ->CreateShaderResourceBinding((Diligent::IShaderResourceBinding **)srb,
                                      true);

    // Link the buffer to the Vertex Shader variable
    auto *v =
        ((Diligent::IShaderResourceBinding *)*srb)
            ->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "Primitives");
    if (v)
      v->Set((Diligent::IBuffer *)*cb);
  }
};
} // namespace Xi

#endif