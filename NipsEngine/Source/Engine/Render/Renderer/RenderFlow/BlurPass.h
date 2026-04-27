#pragma once

#include "RenderPass.h"

class FBlurPass : public FBaseRenderPass
{
public:
	bool Initialize() override;
    bool Release() override;

	bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    TComPtr<ID3D11Texture2D> ShadowBlurTexture;
    TComPtr<ID3D11ShaderResourceView> ShadowBlurSRV;
    TComPtr<ID3D11UnorderedAccessView> ShadowBlurUAV;

    TComPtr<ID3D11Texture2D> ShadowBlurTempTexture;
    TComPtr<ID3D11ShaderResourceView> ShadowBlurTempSRV;
    TComPtr<ID3D11UnorderedAccessView> ShadowBlurTempUAV;
};
