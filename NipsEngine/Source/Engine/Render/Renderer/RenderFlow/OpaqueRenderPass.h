#pragma once
#include "RenderPass.h"
#include "Render/Common/ComPtr.h"

class FOpaqueRenderPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release() override;

protected:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    TComPtr<ID3D11Buffer> VisibleLightConstantBuffer;
	TComPtr<ID3D11Buffer> DirecitonalShadowInfoConstantBuffer;
    TComPtr<ID3D11Buffer> SpotShadowInfoConstantBuffer;
    TComPtr<ID3D11Buffer> SpotShadowConstantsBuffer;
    TComPtr<ID3D11ShaderResourceView> SpotShadowConstantsSRV;
    uint32 SpotShadowConstantsCapacity = 0;
};
