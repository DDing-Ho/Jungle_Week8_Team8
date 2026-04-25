#include "DirectionalLightComponent.h"
#include "Object/ObjectFactory.h"
#include "Engine/Runtime/Engine.h"
#include "Render/Renderer/RenderTarget/DepthStencilBuilder.h"

DEFINE_CLASS(UDirectionalLightComponent, ULightComponent)
REGISTER_FACTORY(UDirectionalLightComponent)

UDirectionalLightComponent::UDirectionalLightComponent()
{
    SetLightType(ELightType::LightType_Directional);
}

void UDirectionalLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    ULightComponent::GetEditableProperties(OutProps);
}

void UDirectionalLightComponent::Serialize(FArchive& Ar)
{
    ULightComponent::Serialize(Ar);
}

void UDirectionalLightComponent::PostDuplicate(UObject* Original)
{
    ULightComponent::PostDuplicate(Original);

    // const UDirectionalLightComponent* Orig = Cast<UDirectionalLightComponent>(Original);
}

void UDirectionalLightComponent::OnRegister()
{
    ULightComponent::OnRegister();

    CreateShadowResources();
}

void UDirectionalLightComponent::OnUnregister()
{
    ULightComponent::OnUnregister();

    ReleaseShadowResources();
}

void UDirectionalLightComponent::CreateShadowResources()
{
    ReleaseShadowResources();

    if (!GEngine)
        return;
    ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
    if (!Device)
        return;

    // DepthStencilBuilder 사용 — 프로젝트에 이미 구현되어 있어 재사용 권장
    FDepthStencilBuilder Builder;
    uint32 Width = static_cast<uint32>(GEngine->GetRenderer().GetFD3DDevice().GetViewportWidth());
    uint32 Height = static_cast<uint32>(GEngine->GetRenderer().GetFD3DDevice().GetViewportHeight());
    Builder.SetSize(Width, Height).WithSRV();

    DepthStencilResource = Builder.Build(Device);
}

void UDirectionalLightComponent::ReleaseShadowResources()
{
    DepthStencilResource.SRV.Reset();
    DepthStencilResource.DSV.Reset();
    DepthStencilResource.Texture.Reset();
}

