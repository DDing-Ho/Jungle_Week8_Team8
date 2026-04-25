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
	OutProps.push_back({ "Shadow Distance", EPropertyType::Float, &ShadowDistance, 1000.0f, 30000.0f, 100.0f });
	OutProps.push_back({ "Cascade Splits", EPropertyType::Vec4, &CascadeSplits });
}

void UDirectionalLightComponent::Serialize(FArchive& Ar)
{
    ULightComponent::Serialize(Ar);
	// Cascade Count는 4로 고정하고 외부에 노출하거나 저장하지 않는다.
	Ar << "ShadowDistance" << ShadowDistance;
	Ar << "CascadeSplits" << CascadeSplits;
}

void UDirectionalLightComponent::PostDuplicate(UObject* Original)
{
    ULightComponent::PostDuplicate(Original);
}

