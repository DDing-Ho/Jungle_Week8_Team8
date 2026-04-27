#pragma once

#include "RenderPass.h"

class FShadowPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
	bool EnsureDirectionalShadowResources(ID3D11Device* Device, uint32 CascadeCount);
    bool EnsureSpotShadowResources(ID3D11Device* Device);

private:
	// ── Cascade Shadow Map (Directional Light) ──────────────────
    static constexpr uint32 DirectionalShadowResolution = 2048;

    TComPtr<ID3D11Texture2D> DirectionalShadowTexture;
    TArray<TComPtr<ID3D11DepthStencilView>> DirectionalShadowDSVs;
    TComPtr<ID3D11ShaderResourceView> DirectionalShadowSRV;
    std::shared_ptr<FShaderBindingInstance> DirectionalShaderBinding;

    // ── Spot Shadow Map ─────────────────────────────────────────
    static constexpr uint32 MaxSpotShadowCount = 8;
    static constexpr uint32 SpotShadowResolution = 1024;

    TComPtr<ID3D11Texture2D> SpotShadowTexture;
    TArray<TComPtr<ID3D11DepthStencilView>> SpotShadowDSVs;
    TComPtr<ID3D11ShaderResourceView> SpotShadowSRV;

	std::shared_ptr<FShaderBindingInstance> ShaderBinding;
};
