#pragma once

#include "RenderPass.h"
#include "ShadowAtlasManager.h"

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
    FShadowAtlasManager ShadowAtlasManager;
	std::shared_ptr<FShaderBindingInstance> ShaderBinding;

	// ── Cascade Shadow Map (Directional Light) ──────────────────
    std::shared_ptr<FShaderBindingInstance> DirectionalShaderBinding;
};
