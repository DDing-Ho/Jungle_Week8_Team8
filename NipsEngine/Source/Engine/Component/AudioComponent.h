#pragma once

#include "ActorComponent.h"
#include "Audio/AudioSystem.h"

class UAudioComponent : public UActorComponent
{
public:
	DECLARE_CLASS(UAudioComponent, UActorComponent)

	UAudioComponent();
	~UAudioComponent() override = default;

	void BeginPlay() override;
	void EndPlay() override;
	void OnUnregister() override;
	void PostDuplicate(UObject* Original) override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	FAudioHandle Play();
	void Stop();
	bool IsPlaying() const;

	const FString& GetSoundPath() const { return SoundPath; }
	void SetSoundPath(const FString& InSoundPath) { SoundPath = InSoundPath; }

	void SetLooping(bool bInLoop) { bLoop = bInLoop; }
	void SetSpatial(bool bInSpatial) { bSpatial = bInSpatial; }
	void SetVolume(float InVolume) { Volume = InVolume; }

protected:
	void TickComponent(float DeltaTime) override;

private:
	FAudioPlayParams MakePlayParams() const;
	FVector GetOwnerLocation() const;

	FString SoundPath;
	bool bPlayOnBeginPlay = true;
	bool bLoop = false;
	bool bSpatial = true;
	float Volume = 1.0f;
	float MinDistance = 1.0f;
	float MaxDistance = 20.0f;
	FAudioHandle PlaybackHandle;
};
