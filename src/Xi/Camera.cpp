#include "../../include/Xi/Camera.hpp"
#include <cstdio>

namespace Xi {

void *Camera3::getView() {
    if (onUpdate.isValid())
        onUpdate();

    usz requiredCount = (usz)(surfaceWidth * surfaceHeight * 4);
    if (surface.size() == 0 || surface.size() != requiredCount) 
           touchGPU();
    
    return surface.deviceView(0); // SRV
}

void Camera3::touchGPU() {
    if (surfaceWidth <= 0) surfaceWidth = 1;
    if (surfaceHeight <= 0) surfaceHeight = 1;
    usz requiredCount = (usz)(surfaceWidth * surfaceHeight * 4);

    // If we have a device, let it manage the surface
    if (device) {
        // check if current surface is already compatible
        if (surface.getDevice() == device && surface.size() == requiredCount)
            return;

        // Allocate 2D surface on device
        void* handle = device->allocSurface(surfaceWidth, surfaceHeight, 4);
        // Wrap it into our string
        surface.wrapDevice(device, handle, requiredCount);
    } else {
        // CPU fallback
        if (surface.size() != requiredCount)
            surface.allocate(requiredCount);
    }
}

void Camera3::render(void *rtv, void *dsv, i32 w, i32 h) {
    if (!root || !rtv)
        return;

    gContext.bindResources(rtv, dsv, w, h);

    f32 aspect = (f32)w / (f32)h;

    Matrix4 viewRotation = Math::multiply(
        Math::rotateY(-getRotation().y),
        Math::rotateX(-getRotation().x)
    );
    Matrix4 viewTranslation = Math::translate(-getPosition().x, -getPosition().y, -getPosition().z);
    Matrix4 view = Math::multiply(viewTranslation, viewRotation);

    Matrix4 proj;
    if (isOrtho) {
        float halfW = (orthoScale * aspect) / 2.0f;
        float halfH = orthoScale / 2.0f;
        proj = Math::ortho(-halfW, halfW, -halfH, halfH, clipStart, clipEnd);
    } else {
        float fovRad = fov * (3.14159f / 180.0f);
        proj = Math::perspective(fovRad, aspect, clipStart, clipEnd);
    }

    if (shiftX != 0.0f || shiftY != 0.0f) {
        Matrix4 shiftMat = Math::translate(shiftX, shiftY, 0.0f);
        proj = Math::multiply(proj, shiftMat);
    }

    Matrix4 vp = Math::multiply(view, proj);
    _renderRec(root, Math::identity(), vp);
}

void Camera3::render() {
    touchGPU();
    void *pRTV_handle = surface.deviceView(1); // RTV
    if (!pRTV_handle)
        return;

    auto *pRTV = (Diligent::ITextureView *)pRTV_handle;
    _ensureDepthBuffer(surfaceWidth, surfaceHeight);

    float clearColor[] = {1.0f, 0.0f, 0.0f, 1.0f};

    gContext.ctx->SetRenderTargets(1, &pRTV, pDSV, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    Diligent::Viewport VP;
    VP.Width = (float)surfaceWidth;
    VP.Height = (float)surfaceHeight;
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    gContext.ctx->SetViewports(1, &VP, 0, 0);

    gContext.ctx->ClearRenderTarget(pRTV, clearColor, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    gContext.ctx->ClearDepthStencil(pDSV, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    render((void *)pRTV, (void *)pDSV, surfaceWidth, surfaceHeight);
}

void Camera3::_ensureDepthBuffer(i32 w, i32 h) {
    if (pDSV) {
        auto *pTex = pDSV->GetTexture();
        if (pTex && pTex->GetDesc().Width == (uint32_t)w && pTex->GetDesc().Height == (uint32_t)h)
            return;
    }

    Diligent::TextureDesc desc;
    desc.Name = "Xi_Camera_InternalDepth";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = (uint32_t)w;
    desc.Height = (uint32_t)h;
    desc.Format = Diligent::TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = Diligent::BIND_DEPTH_STENCIL;

    Diligent::RefCntAutoPtr<Diligent::ITexture> pDepth;
    gContext.device->CreateTexture(desc, nullptr, &pDepth);
    pDSV = pDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
}

void Camera3::_renderRec(TreeItem *n, Matrix4 p, const Matrix4 &vp) {
    if (!n) return;

    Renderable3 *r = dynamic_cast<Renderable3 *>(n);
    Matrix4 world = p;

    if (r) {
        world = Math::multiply(r->getMatrix(), p);

        if (r->mesh && r->shader) {
            r->mesh->upload();
            r->shader->create();

            ShaderData gpuData;
            Matrix4 mvp = Math::multiply(world, vp);
            gpuData.mvp = mvp;
            gpuData.world = world;

            r->shader->updateUniforms(&gpuData, sizeof(ShaderData));
            if (r->shader->_pso == nullptr) {
                printf("Error: Shader PSO is NULL for Renderable %s!\n", r->name.c_str());
                return;
            }
            gContext.setPipelineState(r->shader->_pso);

            if (r->shader->_srb == nullptr) {
                printf("Error: Shader SRB is NULL for Renderable %s!\n", r->name.c_str());
                return;
            }
            auto *srb = (Diligent::IShaderResourceBinding *)r->shader->_srb;
            auto *pTexVar = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture");
            auto *pSamplerVar = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "g_Texture_sampler");

            if (pTexVar && r->surface) {
                void *srv = r->surface->deviceView(0); // View type 0 = SRV
                if (srv)
                    pTexVar->Set((Diligent::ITextureView *)srv);
            }

            if (pSamplerVar) {
                static Diligent::RefCntAutoPtr<Diligent::ISampler> pDefaultSampler;
                if (!pDefaultSampler) {
                    Diligent::SamplerDesc SamDesc;
                    gContext.device->CreateSampler(SamDesc, &pDefaultSampler);
                }
                pSamplerVar->Set(pDefaultSampler);
            }

            gContext.commitResources(r->shader->_srb);
            gContext.drawMesh(r->mesh->_vb, r->mesh->_ib, r->mesh->indices.length());
        }
    }

    for (usz i = 0; i < n->children.length(); ++i)
        _renderRec(n->children[i], world, vp);
}

} // namespace Xi
