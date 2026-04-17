#pragma once
#include "Component/SceneComponent.h"
#include "Render/Common/RenderTypes.h"

class ULightComponent : public USceneComponent
{
public:
	DECLARE_CLASS(ULightComponent, USceneComponent)
	
	ULightComponent();
    ~ULightComponent() override = default;

	/* For Property Window */
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void PostDuplicate(UObject* Original) override;

	void Serialize(FArchive& Ar) override;

	void BeginPlay() override;
	void EndPlay() override;

private:
    FColor LightColor		= {};
    float Intensity			= 1.f;
    bool bEnabled			= true;
    ELightType LightType	= { ELightType::Max };
	// bCastShadows = false;

};
