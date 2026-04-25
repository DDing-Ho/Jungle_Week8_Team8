#pragma once
#include "LightComponent.h"
#include "Render/Renderer/RenderTarget/DepthStencilResource.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_5.h>

class UDirectionalLightComponent : public ULightComponent
{
public:
    DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)

    UDirectionalLightComponent();
    ~UDirectionalLightComponent() override = default;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void Serialize(FArchive& Ar) override;
    void PostDuplicate(UObject* Original) override;

	const FDepthStencilResource& GetDepthStencilResource() { return DepthStencilResource; }

    FString GetVisualizationTexturePath() const override { return "Asset/Texture/Icons/S_LightDirectional.PNG"; }

	void OnRegister() override;
    void OnUnregister() override;

	FMatrix ViewProjectionMatrix = FMatrix::Identity;

private:
    FDepthStencilResource DepthStencilResource;

	void CreateShadowResources();
    void ReleaseShadowResources();
};
