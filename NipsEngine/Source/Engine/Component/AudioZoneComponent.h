#pragma once

#include "Component/SceneComponent.h"
#include "Audio/AudioSystem.h"

class UAudioZoneComponent : public USceneComponent
{
public:
	DECLARE_CLASS(UAudioZoneComponent, USceneComponent)

	UAudioZoneComponent();
	~UAudioZoneComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void OnUnregister() override;
	void PostDuplicate(UObject* Original) override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	const FVector& GetBoxExtent() const { return BoxExtent; }
	FVector GetScaledBoxExtent() const;

protected:
	void TickComponent(float DeltaTime) override;

private:
	bool IsListenerInside() const;
	FVector GetListenerLocation() const;
	void SubmitMix();
	void RemoveMix();
	void ClampEditableValues();

	FVector BoxExtent = FVector(8.0f, 8.0f, 3.0f);
	int32 Priority = 0;
	float FadeInTime = 1.0f;
	float FadeOutTime = 1.0f;
	float MasterVolume = 1.0f;
	float SFXVolume = 1.0f;
	float MusicVolume = 1.0f;
	float AmbientVolume = 1.0f;
	float CurrentWeight = 0.0f;
};
