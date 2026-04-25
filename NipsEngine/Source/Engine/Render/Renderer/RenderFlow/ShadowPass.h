#pragma once

#include "RenderPass.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>

class UDirectionalLightComponent;

struct FShadowConstants
{
	FMatrix LightViewProj;	// Light 시점의 VP 행렬
    FMatrix Model;			// Primitive World 행렬
};



class FShadowPass : public FBaseRenderPass
{
public:
    bool Initialize() override;
    bool Release();

protected:
    bool Begin(const FRenderPassContext* Context) override;
    bool DrawCommand(const FRenderPassContext* Context) override;
    bool End(const FRenderPassContext* Context) override;

private:
    FMatrix GetDirectionalLightViewProj(UDirectionalLightComponent* DirLight);
    void UpdateConstantBuffer(const FRenderPassContext* Context, FShadowConstants ShadowCB);

    TComPtr<ID3D11Buffer> ShadowConstantBuffer;

    TComPtr<ID3D11VertexShader> ShadowVS;
    TComPtr<ID3D11InputLayout> ShadowInputLayout;

};
