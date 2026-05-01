#include "Component/AudioComponent.h"

#include "GameFramework/AActor.h"
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

	if (bPlayOnBeginPlay)
	{
		Play();
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
}

void UAudioComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << "SoundPath" << SoundPath;
	Ar << "PlayOnBeginPlay" << bPlayOnBeginPlay;
	Ar << "Loop" << bLoop;
	Ar << "Spatial" << bSpatial;
	Ar << "Volume" << Volume;
	Ar << "MinDistance" << MinDistance;
	Ar << "MaxDistance" << MaxDistance;
}

void UAudioComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Sound Path", EPropertyType::String, &SoundPath });
	OutProps.push_back({ "Play On BeginPlay", EPropertyType::Bool, &bPlayOnBeginPlay });
	OutProps.push_back({ "Loop", EPropertyType::Bool, &bLoop });
	OutProps.push_back({ "Spatial", EPropertyType::Bool, &bSpatial });
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
}

FAudioHandle UAudioComponent::Play()
{
	if (SoundPath.empty())
	{
		return {};
	}

	Stop();
	PlaybackHandle = FAudioSystem::Get().Play(SoundPath, MakePlayParams());
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
		return;
	}

	if (!FAudioSystem::Get().IsPlaying(PlaybackHandle))
	{
		PlaybackHandle = {};
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
