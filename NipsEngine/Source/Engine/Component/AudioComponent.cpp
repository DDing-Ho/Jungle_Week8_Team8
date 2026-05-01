#include "Component/AudioComponent.h"

#include "Editor/Viewport/ViewportCamera.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Object/ObjectFactory.h"
#include "Serialization/Archive.h"

#include <algorithm>

DEFINE_CLASS(UAudioComponent, UActorComponent)
REGISTER_FACTORY(UAudioComponent)

UAudioComponent::UAudioComponent()
{
	bCanEverTick = true;
}

void UAudioComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	bStartedByStartBehavior = false;

	if (GetStartBehavior() == EAudioStartBehavior::OnBeginPlay)
	{
		Play();
		bStartedByStartBehavior = PlaybackHandle.IsValid();
	}
}

void UAudioComponent::EndPlay()
{
	Stop();
}

void UAudioComponent::OnUnregister()
{
	Stop();
	bRegistered = false;
}

void UAudioComponent::PostDuplicate(UObject* Original)
{
	UActorComponent::PostDuplicate(Original);
	PlaybackHandle = {};
	bPausedByOutsideRange = false;
	bStoppedByOutsideRange = false;
	bStartedByStartBehavior = false;
}

void UAudioComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << "SoundPath" << SoundPath;
	Ar << "StartBehavior" << StartBehavior;
	Ar << "Loop" << bLoop;
	Ar << "Spatial" << bSpatial;
	Ar << "AudioBus" << AudioBus;
	Ar << "OutsideRangeBehavior" << OutsideRangeBehavior;
	Ar << "Volume" << Volume;
	Ar << "MinDistance" << MinDistance;
	Ar << "MaxDistance" << MaxDistance;

	if (Ar.IsLoading())
	{
		PostEditProperty(nullptr);
	}
}

void UAudioComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
	static const char* StartBehaviorNames[] = { "On BeginPlay", "On First In Range", "Manual Only" };
	static const char* OutsideBehaviorNames[] = { "Continue Playing", "Pause And Resume", "Stop And Restart" };
	static const char* AudioBusNames[] = { "SFX", "Music", "Ambient" };
	OutProps.push_back({ "Sound Path", EPropertyType::String, &SoundPath });
	OutProps.push_back({ "Start Behavior", EPropertyType::Enum, &StartBehavior, 0.0f, 0.0f, 0.0f, StartBehaviorNames, 3 });
	OutProps.push_back({ "Loop", EPropertyType::Bool, &bLoop });
	OutProps.push_back({ "Spatial", EPropertyType::Bool, &bSpatial });
	OutProps.push_back({ "Audio Bus", EPropertyType::Enum, &AudioBus, 0.0f, 0.0f, 0.0f, AudioBusNames, 3 });
	OutProps.push_back({ "Outside Range Behavior", EPropertyType::Enum, &OutsideRangeBehavior, 0.0f, 0.0f, 0.0f, OutsideBehaviorNames, 3 });
	OutProps.push_back({ "Volume", EPropertyType::Float, &Volume, 0.0f, 2.0f, 0.01f });
	OutProps.push_back({ "Min Distance", EPropertyType::Float, &MinDistance, 0.01f, 10000.0f, 1.0f });
	OutProps.push_back({ "Max Distance", EPropertyType::Float, &MaxDistance, 0.01f, 10000.0f, 1.0f });
}

void UAudioComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);

	Volume = std::clamp(Volume, 0.0f, 2.0f);
	MinDistance = std::max(0.01f, MinDistance);
	MaxDistance = std::max(MinDistance, MaxDistance);
	StartBehavior = std::clamp(StartBehavior, 0, static_cast<int32>(EAudioStartBehavior::Count) - 1);
	AudioBus = std::clamp(AudioBus, 0, static_cast<int32>(EAudioBus::Count) - 1);
	OutsideRangeBehavior = std::clamp(OutsideRangeBehavior, 0, static_cast<int32>(EAudioOutsideBehavior::Count) - 1);
}

FAudioHandle UAudioComponent::Play()
{
	if (SoundPath.empty())
	{
		return {};
	}

	Stop();
	PlaybackHandle = FAudioSystem::Get().Play(SoundPath, MakePlayParams());
	bPausedByOutsideRange = false;
	bStoppedByOutsideRange = false;
	bStartedByStartBehavior = PlaybackHandle.IsValid();
	return PlaybackHandle;
}

void UAudioComponent::Pause()
{
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Pause(PlaybackHandle);
	}
}

void UAudioComponent::Resume()
{
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Resume(PlaybackHandle);
	}
	else
	{
		Play();
	}
}

void UAudioComponent::Restart()
{
	bPausedByOutsideRange = false;
	bStoppedByOutsideRange = false;
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Restart(PlaybackHandle);
	}
	else
	{
		Play();
	}
}

void UAudioComponent::Stop()
{
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().Stop(PlaybackHandle);
		PlaybackHandle = {};
	}
	bPausedByOutsideRange = false;
	bStoppedByOutsideRange = false;
}

bool UAudioComponent::IsPlaying() const
{
	return PlaybackHandle.IsValid() && FAudioSystem::Get().IsPlaying(PlaybackHandle);
}

void UAudioComponent::SetPlaybackTime(float TimeSeconds)
{
	if (PlaybackHandle.IsValid())
	{
		FAudioSystem::Get().SetPlaybackTime(PlaybackHandle, TimeSeconds);
	}
}

float UAudioComponent::GetPlaybackTime() const
{
	return PlaybackHandle.IsValid() ? FAudioSystem::Get().GetPlaybackTime(PlaybackHandle) : 0.0f;
}

float UAudioComponent::GetDuration() const
{
	if (PlaybackHandle.IsValid())
	{
		const float ActiveDuration = FAudioSystem::Get().GetDuration(PlaybackHandle);
		if (ActiveDuration > 0.0f)
		{
			return ActiveDuration;
		}
	}

	return FAudioSystem::Get().GetSoundDuration(SoundPath);
}

void UAudioComponent::TickComponent(float DeltaTime)
{
	(void)DeltaTime;

	if (!PlaybackHandle.IsValid())
	{
		if (bStoppedByOutsideRange && !IsListenerOutsideMaxDistance())
		{
			bStoppedByOutsideRange = false;
			Play();
		}
		else if (ShouldAutoStart())
		{
			Play();
		}
		return;
	}

	const EAudioOutsideBehavior OutsideBehavior = GetOutsideRangeBehavior();
	const bool bOutsideRange = IsListenerOutsideMaxDistance();
	if (bOutsideRange)
	{
		if (OutsideBehavior == EAudioOutsideBehavior::PauseAndResume)
		{
			if (FAudioSystem::Get().IsPlaying(PlaybackHandle))
			{
				FAudioSystem::Get().Pause(PlaybackHandle);
				bPausedByOutsideRange = true;
			}
			return;
		}

		if (OutsideBehavior == EAudioOutsideBehavior::StopAndRestart)
		{
			FAudioSystem::Get().Stop(PlaybackHandle);
			PlaybackHandle = {};
			bPausedByOutsideRange = false;
			bStoppedByOutsideRange = true;
			return;
		}
	}
	else if (bPausedByOutsideRange)
	{
		FAudioSystem::Get().Resume(PlaybackHandle);
		bPausedByOutsideRange = false;
	}

	if (!FAudioSystem::Get().IsPlaying(PlaybackHandle))
	{
		PlaybackHandle = {};
		bPausedByOutsideRange = false;
		return;
	}

	if (bSpatial)
	{
		FAudioSystem::Get().SetSoundPosition(PlaybackHandle, GetOwnerLocation());
	}
}

FAudioPlayParams UAudioComponent::MakePlayParams() const
{
	FAudioPlayParams Params;
	Params.bLoop = bLoop;
	Params.bSpatial = bSpatial;
	Params.Bus = static_cast<EAudioBus>(AudioBus);
	Params.Volume = Volume;
	Params.MinDistance = MinDistance;
	Params.MaxDistance = MaxDistance;
	Params.Location = GetOwnerLocation();
	return Params;
}

FVector UAudioComponent::GetOwnerLocation() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
}

FVector UAudioComponent::GetListenerLocation() const
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = OwnerActor ? OwnerActor->GetFocusedWorld() : nullptr;
	const FViewportCamera* Camera = World ? World->GetActiveCamera() : nullptr;
	return Camera ? Camera->GetLocation() : GetOwnerLocation();
}

bool UAudioComponent::IsListenerOutsideMaxDistance() const
{
	if (!bSpatial || MaxDistance <= 0.0f)
	{
		return false;
	}

	return FVector::Distance(GetListenerLocation(), GetOwnerLocation()) > MaxDistance;
}

EAudioOutsideBehavior UAudioComponent::GetOutsideRangeBehavior() const
{
	const int32 ClampedValue = std::clamp(
		OutsideRangeBehavior,
		0,
		static_cast<int32>(EAudioOutsideBehavior::Count) - 1);
	return static_cast<EAudioOutsideBehavior>(ClampedValue);
}

EAudioStartBehavior UAudioComponent::GetStartBehavior() const
{
	const int32 ClampedValue = std::clamp(
		StartBehavior,
		0,
		static_cast<int32>(EAudioStartBehavior::Count) - 1);
	return static_cast<EAudioStartBehavior>(ClampedValue);
}

bool UAudioComponent::ShouldAutoStart() const
{
	if (bStartedByStartBehavior || SoundPath.empty())
	{
		return false;
	}

	const EAudioStartBehavior Behavior = GetStartBehavior();
	if (Behavior != EAudioStartBehavior::OnFirstEnter)
	{
		return false;
	}

	return !IsListenerOutsideMaxDistance();
}
