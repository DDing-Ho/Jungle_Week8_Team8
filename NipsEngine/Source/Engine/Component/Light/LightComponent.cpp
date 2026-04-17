#include "LightComponent.h"
#include "Object/ObjectFactory.h"
DEFINE_CLASS(ULightComponent, USceneComponent)
REGISTER_FACTORY(ULightComponent)

ULightComponent::ULightComponent() = default;

void ULightComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    USceneComponent::GetEditableProperties(OutProps);

	OutProps.push_back({ "LightColor", EPropertyType::Color, &LightColor });
    OutProps.push_back({ "Intensity", EPropertyType::Float, &Intensity });
    OutProps.push_back({ "Enable", EPropertyType::Bool, &bEnabled });
}

void ULightComponent::PostEditProperty(const char* PropertyName)
{
    USceneComponent::PostEditProperty(PropertyName);
}

void ULightComponent::Serialize(FArchive& Ar)
{
    USceneComponent::Serialize(Ar);
}

void ULightComponent::BeginPlay()
{
    USceneComponent::BeginPlay();
}

void ULightComponent::EndPlay()
{
    USceneComponent::EndPlay();
}

void ULightComponent::PostDuplicate(UObject* Original)
{
    USceneComponent::PostDuplicate(Original);

	const ULightComponent* Orig = Cast<ULightComponent>(Original);
	
}