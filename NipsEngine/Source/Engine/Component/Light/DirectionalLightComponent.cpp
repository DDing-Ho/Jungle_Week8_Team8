#include "DirectionalLightComponent.h"
#include "Object/ObjectFactory.h"

DEFINE_CLASS(UDirectionalLightComponent, ULightComponent)
REGISTER_FACTORY(UDirectionalLightComponent)

UDirectionalLightComponent::UDirectionalLightComponent()
{
    SetLightType(ELightType::LightType_Directional);
}

void UDirectionalLightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    ULightComponent::GetEditableProperties(OutProps);
	// Cascade Count는 4로 고정하고 외부에 노출하거나 저장하지 않는다.
	OutProps.push_back({ "Shadow Distance", EPropertyType::Float, &ShadowDistance, 500.0f, 30000.0f, 100.0f });
	OutProps.push_back({ "Cascade Split Weight", EPropertyType::Float, &CascadeSplitWeight, 0.0f, 1.0f, 0.01f });
	OutProps.push_back({ "Shadow Texel Snapped", EPropertyType::Bool, &bShadowTexelSnapped });
}

void UDirectionalLightComponent::Serialize(FArchive& Ar)
{
    ULightComponent::Serialize(Ar);
	// Cascade Count는 4로 고정하고 외부에 노출하거나 저장하지 않는다.
	Ar << "ShadowDistance" << ShadowDistance;
	Ar << "CascadeSplitWeight" << CascadeSplitWeight;
	Ar << "bShadowTexelSnapped" << bShadowTexelSnapped;
}

void UDirectionalLightComponent::PostDuplicate(UObject* Original)
{
    ULightComponent::PostDuplicate(Original);

    const UDirectionalLightComponent* Orig = Cast<UDirectionalLightComponent>(Original);
    if (!Orig) { return; }

    ShadowDistance = Orig->ShadowDistance;
    CascadeSplitWeight = Orig->CascadeSplitWeight;
}

