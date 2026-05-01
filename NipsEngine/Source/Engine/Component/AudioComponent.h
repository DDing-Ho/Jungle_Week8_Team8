#pragma once

#include "Component/SceneComponent.h"
#include "Audio/AudioSystem.h"

class UAudioComponent : public USceneComponent
{
public:
	DECLARE_CLASS(UAudioComponent, USceneComponent)

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
	void Pause();
	void Resume();
	void Restart();
	void Stop();
	bool IsPlaying() const;
	bool HasPlayback() const { return PlaybackHandle.IsValid(); }
	void SetPlaybackTime(float TimeSeconds);
	float GetPlaybackTime() const;
	float GetDuration() const;
	FAudioHandle PlayPreview();
	void PausePreview();
	void ResumePreview();
	void RestartPreview();
	void StopPreview();
	bool IsPreviewPlaying() const;
	bool HasPreviewPlayback() const { return PreviewPlaybackHandle.IsValid(); }
	void SetPreviewPlaybackTime(float TimeSeconds);
	float GetPreviewPlaybackTime() const;
	float GetPreviewDuration() const;

	const FString& GetSoundPath() const { return SoundPath; }
	void SetSoundPath(const FString& InSoundPath) { SoundPath = InSoundPath; }
	bool IsSpatial() const { return bSpatial; }
	float GetMinDistance() const { return MinDistance; }
	float GetMaxDistance() const { return MaxDistance; }

	void SetLooping(bool bInLoop) { bLoop = bInLoop; }
	void SetSpatial(bool bInSpatial) { bSpatial = bInSpatial; }
	void SetVolume(float InVolume) { Volume = InVolume; }

protected:
	void TickComponent(float DeltaTime) override;

private:
	FAudioPlayParams MakePlayParams() const;
	FVector GetAudioLocation() const;
	FVector GetListenerLocation() const;
	bool IsListenerOutsideMaxDistance() const;
	EAudioOutsideBehavior GetOutsideRangeBehavior() const;
	EAudioStartBehavior GetStartBehavior() const;
	bool ShouldAutoStart() const;

	FString SoundPath;
	int32 StartBehavior = static_cast<int32>(EAudioStartBehavior::OnBeginPlay);
	bool bLoop = false;
	bool bSpatial = true;
	int32 AudioBus = static_cast<int32>(EAudioBus::SFX);
	int32 OutsideRangeBehavior = static_cast<int32>(EAudioOutsideBehavior::ContinuePlaying);
	float Volume = 1.0f;
	float MinDistance = 1.0f;
	float MaxDistance = 8.0f;
	bool bPausedByOutsideRange = false;
	bool bStoppedByOutsideRange = false;
	bool bStartedByStartBehavior = false;
	FAudioHandle PlaybackHandle;
	FAudioHandle PreviewPlaybackHandle;
};
