/**
 * @file Shader.hpp
 * @brief Shader management and GPU pipeline state for the Xi Graphics module.

 */

#ifndef XI_GRAPHICS_SHADER_HPP
#define XI_GRAPHICS_SHADER_HPP

#include "Graphics.hpp"

/**
 * @namespace Graphics
 * @brief Contains rendering, windowing, and graphics primitives.
 */
namespace Graphics {

using namespace Xi;

/**
 * @struct Shader
 * @brief Represents a GPU shader program and its associated pipeline state.
 */
struct XI_EXPORT Shader {
  String vertexSource;  ///< HLSL vertex shader source code.
  String pixelSource;   ///< HLSL pixel shader source code.
  void *_pso = nullptr; ///< Pointer to the Pipeline State Object (PSO).
  void *_srb = nullptr; ///< Pointer to the Shader Resource Binding (SRB).
  void *_cb = nullptr;  ///< Pointer to the Constant Buffer (CB) for uniforms.

  /**
   * @brief Compiles the shader and creates the GPU pipeline state.
   */
  void create() {
    if (_pso)
      return;
    createShader(vertexSource.c_str(), pixelSource.c_str(), &_pso, &_srb, &_cb);
  }

  /**
   * @brief Updates the uniform data in the constant buffer.
   * @param d Pointer to the data.
   * @param s Size of the data in bytes.
   */
  void updateUniforms(const void *d, u32 s) {
    void *m = gContext.mapBuffer(_cb);
    if (m) {
      memcpy(m, d, s);
      gContext.unmapBuffer(_cb);
    }
  }

  /**
   * @brief Destructor ensures GPU resources are released.
   */
  ~Shader() {
    gContext.release(_pso);
    gContext.release(_srb);
    gContext.release(_cb);
  }

private:
  /**
   * @brief Internal helper to initialize Diligent Engine shader objects.
   */
  void createShader(const char *vs, const char *ps, void **pso, void **srb,
                    void **cb) {
    Diligent::ShaderCreateInfo CI;
    CI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_HLSL;
    Diligent::RefCntAutoPtr<Diligent::IShader> VS, PS;

    CI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    CI.Desc.Name = "VS";
    CI.EntryPoint = "main";
    CI.Source = vs;
    Diligent::IShader *pVS_raw = nullptr;
    gContext.device->CreateShader(CI, &pVS_raw);
    VS.Attach(pVS_raw);
    if (!VS)
      return;

    CI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    CI.Desc.Name = "PS";
    CI.EntryPoint = "main";
    CI.Source = ps;
    Diligent::IShader *pPS_raw = nullptr;
    gContext.device->CreateShader(CI, &pPS_raw);
    PS.Attach(pPS_raw);
    if (!PS)
      return;

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

    Diligent::LayoutElement LayoutElems[] = {
        {0, 0, 3, Diligent::VT_FLOAT32, false, 0xFFFFFFFF},
        {1, 0, 2, Diligent::VT_FLOAT32, false, 0xFFFFFFFF},
        {2, 0, 3, Diligent::VT_FLOAT32, false, 0xFFFFFFFF},
        {3, 0, 4, Diligent::VT_UINT32, false, 0xFFFFFFFF},
        {4, 0, 4, Diligent::VT_FLOAT32, false, 0xFFFFFFFF}};

    P.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
    P.GraphicsPipeline.InputLayout.NumElements = 5;
    P.pVS = VS;
    P.pPS = PS;

    P.PSODesc.ResourceLayout.DefaultVariableType =
        Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;
    gContext.device->CreateGraphicsPipelineState(
        P, (Diligent::IPipelineState **)pso);

    Diligent::BufferDesc CD;
    CD.Size = 128; // Space for g_MVP and g_Model
    CD.Usage = Diligent::USAGE_DYNAMIC;
    CD.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    CD.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    gContext.device->CreateBuffer(CD, nullptr, (Diligent::IBuffer **)cb);

    ((Diligent::IPipelineState *)*pso)
        ->CreateShaderResourceBinding((Diligent::IShaderResourceBinding **)srb,
                                      true);

    auto *v =
        ((Diligent::IShaderResourceBinding *)*srb)
            ->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "Primitives");
    if (v)
      v->Set((Diligent::IBuffer *)*cb);
  }
};

} // namespace Graphics

#endif // XI_GRAPHICS_SHADER_HPP
