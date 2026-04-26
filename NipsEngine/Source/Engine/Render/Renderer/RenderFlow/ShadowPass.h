#pragma once

#include "RenderPass.h"

class FShadowRenderPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    bool EnsureSpotShadowResources(ID3D11Device* Device);

private:
    static constexpr uint32 MaxSpotShadowCount = 8;
    static constexpr uint32 SpotShadowResolution = 1024;

    TComPtr<ID3D11Texture2D> SpotShadowTexture;
    TArray<TComPtr<ID3D11DepthStencilView>> SpotShadowDSVs;
    TComPtr<ID3D11ShaderResourceView> SpotShadowSRV;

	std::shared_ptr<FShaderBindingInstance> ShaderBinding;
};
