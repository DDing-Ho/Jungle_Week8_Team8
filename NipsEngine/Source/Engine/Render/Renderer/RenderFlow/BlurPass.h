#pragma once

#include "RenderPass.h"

struct FShadowBlurConstants
{
    uint32 BlurDirection; // 0 = Horizontal, 1 = Vertical
    uint32 SliceCount;
    uint32 Pad0, Pad1;
};

class FBlurPass : public FBaseRenderPass
{
public:
	bool Initialize() override;
    bool Release() override;

	bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    bool EnsureComputeShader(ID3D11Device* Device);
    bool EnsureConstantBuffer(ID3D11Device* Device);
    bool EnsureShadowBlurResources(ID3D11Device* Device);

	void UpdateConstantBuffer(ID3D11DeviceContext* DeviceContext, uint32 BlurDirection);

private:
    static constexpr uint32 MaxSpotShadowCount = 8;
    static constexpr uint32 SpotShadowResolution = 1024;

	TComPtr<ID3D11ComputeShader> ComputeShader;
    TComPtr<ID3D11Buffer> ConstantBuffer;

	TComPtr<ID3D11ShaderResourceView> ShadowVSMInputSRV;

	// Horizontal Blur 중간 결과
	TComPtr<ID3D11Texture2D> ShadowBlurTempTexture;
    TComPtr<ID3D11ShaderResourceView> ShadowBlurTempSRV;
    TComPtr<ID3D11UnorderedAccessView> ShadowBlurTempUAV;

    // Vertical Blur 최종 결과
    TComPtr<ID3D11Texture2D> ShadowBlurFinalTexture;
    TComPtr<ID3D11ShaderResourceView> ShadowBlurFinalSRV;
    TComPtr<ID3D11UnorderedAccessView> ShadowBlurFinalUAV;
};
