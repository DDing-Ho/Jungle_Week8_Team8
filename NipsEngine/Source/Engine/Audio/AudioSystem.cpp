#include "Audio/AudioSystem.h"

#include "UI/EditorConsoleWidget.h"

#include <memory>
#include <cmath>

#if __has_include(<miniaudio.h>)
	#define NIPS_WITH_MINIAUDIO 1
	#define MINIAUDIO_IMPLEMENTATION
	#pragma warning(push)
	#pragma warning(disable: 4244)
	#include <miniaudio.h>
	#pragma warning(pop)
	#include "Core/Paths.h"
	#include <algorithm>
	#include <filesystem>
	#include <unordered_map>
#else
	#define NIPS_WITH_MINIAUDIO 0
#endif

#if NIPS_WITH_MINIAUDIO
namespace
{
	float ClampVolume(float Value)
	{
		if (Value < 0.0f) return 0.0f;
		if (Value > 2.0f) return 2.0f;
		return Value;
	}

	int32 ToBusIndex(EAudioBus Bus)
	{
		const int32 Index = static_cast<int32>(Bus);
		if (Index < 0 || Index >= static_cast<int32>(EAudioBus::Count))
		{
			return static_cast<int32>(EAudioBus::SFX);
		}
		return Index;
	}

	FVector ToAudioVector(const FVector& WorldVector)
	{
		return FVector(WorldVector.X, -WorldVector.Y, WorldVector.Z);
	}

	FString ResolveAudioPath(const FString& SoundPath)
	{
		if (SoundPath.empty())
		{
			return {};
		}

		std::filesystem::path Path(FPaths::ToWide(SoundPath));
		if (Path.is_absolute())
		{
			return FPaths::ToUtf8(Path.wstring());
		}

		return FPaths::ToAbsoluteString(FPaths::ToWide(SoundPath));
	}
}

struct FAudioSystemImpl
{
	struct FActiveSound
	{
		std::unique_ptr<ma_sound> Sound;
		bool bLoop = false;
		EAudioBus Bus = EAudioBus::SFX;
		float BaseVolume = 1.0f;
	};

	struct FZoneMix
	{
		int32 Priority = 0;
		float Weight = 0.0f;
		float MasterVolume = 1.0f;
		float SFXVolume = 1.0f;
		float MusicVolume = 1.0f;
		float AmbientVolume = 1.0f;
	};

	ma_engine Engine{};
	bool bInitialized = false;
	uint32 NextHandleId = 1;
	std::unordered_map<uint32, FActiveSound> ActiveSounds;
	std::unordered_map<uint32, FZoneMix> ZoneMixes;
	float MasterVolume = 1.0f;
	float BusVolumes[static_cast<int32>(EAudioBus::Count)] = { 1.0f, 1.0f, 1.0f };

	float GetEffectiveVolume(float BaseVolume, EAudioBus Bus) const
	{
		return ClampVolume(BaseVolume * MasterVolume * BusVolumes[ToBusIndex(Bus)]);
	}

	void ApplyVolume(FActiveSound& ActiveSound)
	{
		if (ActiveSound.Sound)
		{
			ma_sound_set_volume(ActiveSound.Sound.get(), GetEffectiveVolume(ActiveSound.BaseVolume, ActiveSound.Bus));
		}
	}

	void ApplyVolumes()
	{
		for (auto& Pair : ActiveSounds)
		{
			ApplyVolume(Pair.second);
		}
	}

	void EvaluateZoneMixes()
	{
		FZoneMix BestZone;
		bool bHasBestZone = false;

		for (const auto& Pair : ZoneMixes)
		{
			const FZoneMix& Zone = Pair.second;
			if (Zone.Weight <= 0.0f)
			{
				continue;
			}

			if (!bHasBestZone ||
				Zone.Priority > BestZone.Priority ||
				(Zone.Priority == BestZone.Priority && Zone.Weight > BestZone.Weight))
			{
				BestZone = Zone;
				bHasBestZone = true;
			}
		}

		const float Weight = bHasBestZone ? std::clamp(BestZone.Weight, 0.0f, 1.0f) : 0.0f;
		const float NewMaster = bHasBestZone ? 1.0f + (BestZone.MasterVolume - 1.0f) * Weight : 1.0f;
		const float NewSFX = bHasBestZone ? 1.0f + (BestZone.SFXVolume - 1.0f) * Weight : 1.0f;
		const float NewMusic = bHasBestZone ? 1.0f + (BestZone.MusicVolume - 1.0f) * Weight : 1.0f;
		const float NewAmbient = bHasBestZone ? 1.0f + (BestZone.AmbientVolume - 1.0f) * Weight : 1.0f;

		if (std::fabs(MasterVolume - NewMaster) <= 0.0001f &&
			std::fabs(BusVolumes[0] - NewSFX) <= 0.0001f &&
			std::fabs(BusVolumes[1] - NewMusic) <= 0.0001f &&
			std::fabs(BusVolumes[2] - NewAmbient) <= 0.0001f)
		{
			return;
		}

		MasterVolume = ClampVolume(NewMaster);
		BusVolumes[0] = ClampVolume(NewSFX);
		BusVolumes[1] = ClampVolume(NewMusic);
		BusVolumes[2] = ClampVolume(NewAmbient);
		ApplyVolumes();
	}
};
#else
struct FAudioSystemImpl
{
	bool bLoggedDisabled = false;
};
#endif

FAudioSystem::FAudioSystem()
	: Impl(std::make_unique<FAudioSystemImpl>())
{
}

FAudioSystem::~FAudioSystem()
{
	Shutdown();
}

bool FAudioSystem::Init()
{
#if NIPS_WITH_MINIAUDIO
	if (Impl->bInitialized)
	{
		return true;
	}

	ma_engine_config Config = ma_engine_config_init();
	const ma_result Result = ma_engine_init(&Config, &Impl->Engine);
	if (Result != MA_SUCCESS)
	{
		UE_LOG("AudioSystem: failed to initialize miniaudio engine. error=%d", static_cast<int>(Result));
		return false;
	}

	Impl->bInitialized = true;
	UE_LOG("AudioSystem: initialized.");
	return true;
#else
	if (!Impl->bLoggedDisabled)
	{
		Impl->bLoggedDisabled = true;
		UE_LOG("AudioSystem: miniaudio.h was not found. Run vcpkg install to enable audio.");
	}
	return false;
#endif
}

void FAudioSystem::Shutdown()
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl || !Impl->bInitialized)
	{
		return;
	}

	StopAll();
	Impl->ZoneMixes.clear();
	Impl->MasterVolume = 1.0f;
	Impl->BusVolumes[0] = 1.0f;
	Impl->BusVolumes[1] = 1.0f;
	Impl->BusVolumes[2] = 1.0f;
	ma_engine_uninit(&Impl->Engine);
	Impl->bInitialized = false;
	UE_LOG("AudioSystem: shutdown.");
#endif
}

void FAudioSystem::Tick(float DeltaTime)
{
	(void)DeltaTime;

#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized)
	{
		return;
	}

	Impl->EvaluateZoneMixes();

	for (auto It = Impl->ActiveSounds.begin(); It != Impl->ActiveSounds.end();)
	{
		ma_sound* Sound = It->second.Sound.get();
		if (!Sound || (!It->second.bLoop && ma_sound_at_end(Sound)))
		{
			if (Sound)
			{
				ma_sound_uninit(Sound);
			}
			It = Impl->ActiveSounds.erase(It);
			continue;
		}

		++It;
	}
#endif
}

bool FAudioSystem::IsInitialized() const
{
#if NIPS_WITH_MINIAUDIO
	return Impl && Impl->bInitialized;
#else
	return false;
#endif
}

FAudioHandle FAudioSystem::Play2D(const FString& SoundPath, float Volume, bool bLoop)
{
	FAudioPlayParams Params;
	Params.bSpatial = false;
	Params.bLoop = bLoop;
	Params.Volume = Volume;
	return Play(SoundPath, Params);
}

FAudioHandle FAudioSystem::PlayAtLocation(const FString& SoundPath, const FVector& Location, float Volume,
	bool bLoop, float MinDistance, float MaxDistance)
{
	FAudioPlayParams Params;
	Params.bSpatial = true;
	Params.bLoop = bLoop;
	Params.Volume = Volume;
	Params.Location = Location;
	Params.MinDistance = MinDistance;
	Params.MaxDistance = MaxDistance;
	return Play(SoundPath, Params);
}

FAudioHandle FAudioSystem::Play(const FString& SoundPath, const FAudioPlayParams& Params)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized && !Init())
	{
		return {};
	}

	const FString AbsolutePath = ResolveAudioPath(SoundPath);
	if (AbsolutePath.empty())
	{
		UE_LOG("AudioSystem: empty sound path.");
		return {};
	}

	if (!std::filesystem::exists(std::filesystem::path(FPaths::ToWide(AbsolutePath))))
	{
		UE_LOG("AudioSystem: sound file not found: %s", SoundPath.c_str());
		return {};
	}

	auto Sound = std::make_unique<ma_sound>();
	const ma_uint32 Flags = 0;
	ma_result Result = ma_sound_init_from_file(&Impl->Engine, AbsolutePath.c_str(), Flags, nullptr, nullptr, Sound.get());
	if (Result != MA_SUCCESS)
	{
		UE_LOG("AudioSystem: failed to load sound '%s'. error=%d", SoundPath.c_str(), static_cast<int>(Result));
		return {};
	}

	const float BaseVolume = ClampVolume(Params.Volume);
	ma_sound_set_volume(Sound.get(), Impl->GetEffectiveVolume(BaseVolume, Params.Bus));
	ma_sound_set_looping(Sound.get(), Params.bLoop ? MA_TRUE : MA_FALSE);
	ma_sound_set_spatialization_enabled(Sound.get(), Params.bSpatial ? MA_TRUE : MA_FALSE);

	if (Params.bSpatial)
	{
		const float MinDistance = std::max(0.01f, Params.MinDistance);
		const float MaxDistance = std::max(MinDistance, Params.MaxDistance);
		const FVector AudioLocation = ToAudioVector(Params.Location);
		ma_sound_set_position(Sound.get(), AudioLocation.X, AudioLocation.Y, AudioLocation.Z);
		ma_sound_set_attenuation_model(Sound.get(), ma_attenuation_model_linear);
		ma_sound_set_min_distance(Sound.get(), MinDistance);
		ma_sound_set_max_distance(Sound.get(), MaxDistance);
	}

	Result = ma_sound_start(Sound.get());
	if (Result != MA_SUCCESS)
	{
		UE_LOG("AudioSystem: failed to start sound '%s'. error=%d", SoundPath.c_str(), static_cast<int>(Result));
		ma_sound_uninit(Sound.get());
		return {};
	}

	FAudioHandle Handle;
	Handle.Id = Impl->NextHandleId++;
	if (Impl->NextHandleId == 0)
	{
		Impl->NextHandleId = 1;
	}

	Impl->ActiveSounds[Handle.Id] = FAudioSystemImpl::FActiveSound{ std::move(Sound), Params.bLoop, Params.Bus, BaseVolume };
	return Handle;
#else
	(void)SoundPath;
	(void)Params;
	Init();
	return {};
#endif
}

void FAudioSystem::Stop(FAudioHandle Handle)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end())
	{
		return;
	}

	ma_sound* Sound = It->second.Sound.get();
	if (Sound)
	{
		ma_sound_stop(Sound);
		ma_sound_uninit(Sound);
	}

	Impl->ActiveSounds.erase(It);
#else
	(void)Handle;
#endif
}

void FAudioSystem::Pause(FAudioHandle Handle)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	ma_sound_stop(It->second.Sound.get());
#else
	(void)Handle;
#endif
}

void FAudioSystem::Resume(FAudioHandle Handle)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	ma_sound_start(It->second.Sound.get());
#else
	(void)Handle;
#endif
}

void FAudioSystem::Restart(FAudioHandle Handle)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	ma_sound_seek_to_pcm_frame(It->second.Sound.get(), 0);
	ma_sound_start(It->second.Sound.get());
#else
	(void)Handle;
#endif
}

void FAudioSystem::StopAll()
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl)
	{
		return;
	}

	for (auto& Pair : Impl->ActiveSounds)
	{
		ma_sound* Sound = Pair.second.Sound.get();
		if (Sound)
		{
			ma_sound_stop(Sound);
			ma_sound_uninit(Sound);
		}
	}

	Impl->ActiveSounds.clear();
#endif
}

bool FAudioSystem::IsPlaying(FAudioHandle Handle) const
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return false;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return false;
	}

	return ma_sound_is_playing(It->second.Sound.get()) == MA_TRUE;
#else
	(void)Handle;
	return false;
#endif
}

void FAudioSystem::SetVolume(FAudioHandle Handle, float Volume)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	It->second.BaseVolume = ClampVolume(Volume);
	Impl->ApplyVolume(It->second);
#else
	(void)Handle;
	(void)Volume;
#endif
}

void FAudioSystem::SetPlaybackTime(FAudioHandle Handle, float TimeSeconds)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	const float Duration = GetDuration(Handle);
	if (Duration > 0.0f)
	{
		TimeSeconds = std::clamp(TimeSeconds, 0.0f, Duration);
	}
	else
	{
		TimeSeconds = std::max(0.0f, TimeSeconds);
	}

	ma_sound_seek_to_second(It->second.Sound.get(), TimeSeconds);
#else
	(void)Handle;
	(void)TimeSeconds;
#endif
}

float FAudioSystem::GetPlaybackTime(FAudioHandle Handle) const
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return 0.0f;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return 0.0f;
	}

	float Cursor = 0.0f;
	if (ma_sound_get_cursor_in_seconds(It->second.Sound.get(), &Cursor) != MA_SUCCESS)
	{
		return 0.0f;
	}
	return Cursor;
#else
	(void)Handle;
	return 0.0f;
#endif
}

float FAudioSystem::GetDuration(FAudioHandle Handle) const
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return 0.0f;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return 0.0f;
	}

	float Duration = 0.0f;
	if (ma_sound_get_length_in_seconds(It->second.Sound.get(), &Duration) != MA_SUCCESS)
	{
		return 0.0f;
	}
	return Duration;
#else
	(void)Handle;
	return 0.0f;
#endif
}

float FAudioSystem::GetSoundDuration(const FString& SoundPath) const
{
#if NIPS_WITH_MINIAUDIO
	const FString AbsolutePath = ResolveAudioPath(SoundPath);
	if (AbsolutePath.empty() || !std::filesystem::exists(std::filesystem::path(FPaths::ToWide(AbsolutePath))))
	{
		return 0.0f;
	}

	ma_decoder Decoder{};
	if (ma_decoder_init_file(AbsolutePath.c_str(), nullptr, &Decoder) != MA_SUCCESS)
	{
		return 0.0f;
	}

	ma_uint64 LengthInFrames = 0;
	const ma_result Result = ma_decoder_get_length_in_pcm_frames(&Decoder, &LengthInFrames);
	const float Duration = (Result == MA_SUCCESS && Decoder.outputSampleRate > 0)
		? static_cast<float>(static_cast<double>(LengthInFrames) / static_cast<double>(Decoder.outputSampleRate))
		: 0.0f;
	ma_decoder_uninit(&Decoder);
	return Duration;
#else
	(void)SoundPath;
	return 0.0f;
#endif
}

void FAudioSystem::SetSoundPosition(FAudioHandle Handle, const FVector& Location)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized || !Handle.IsValid())
	{
		return;
	}

	auto It = Impl->ActiveSounds.find(Handle.Id);
	if (It == Impl->ActiveSounds.end() || !It->second.Sound)
	{
		return;
	}

	const FVector AudioLocation = ToAudioVector(Location);
	ma_sound_set_position(It->second.Sound.get(), AudioLocation.X, AudioLocation.Y, AudioLocation.Z);
#else
	(void)Handle;
	(void)Location;
#endif
}

void FAudioSystem::SetListenerTransform(const FVector& Location, const FVector& Forward, const FVector& Up)
{
#if NIPS_WITH_MINIAUDIO
	if (!Impl->bInitialized)
	{
		return;
	}

	const FVector AudioLocation = ToAudioVector(Location);
	const FVector SafeForward = ToAudioVector(Forward).GetSafeNormal();
	const FVector SafeUp = ToAudioVector(Up).GetSafeNormal();
	ma_engine_listener_set_position(&Impl->Engine, 0, AudioLocation.X, AudioLocation.Y, AudioLocation.Z);
	ma_engine_listener_set_direction(&Impl->Engine, 0, SafeForward.X, SafeForward.Y, SafeForward.Z);
	ma_engine_listener_set_world_up(&Impl->Engine, 0, SafeUp.X, SafeUp.Y, SafeUp.Z);
#else
	(void)Location;
	(void)Forward;
	(void)Up;
#endif
}

void FAudioSystem::SubmitZoneMix(uint32 ZoneId, int32 Priority, float Weight,
	float MasterVolume, float SFXVolume, float MusicVolume, float AmbientVolume)
{
#if NIPS_WITH_MINIAUDIO
	if (ZoneId == 0)
	{
		return;
	}

	FAudioSystemImpl::FZoneMix& Zone = Impl->ZoneMixes[ZoneId];
	Zone.Priority = Priority;
	Zone.Weight = std::clamp(Weight, 0.0f, 1.0f);
	Zone.MasterVolume = ClampVolume(MasterVolume);
	Zone.SFXVolume = ClampVolume(SFXVolume);
	Zone.MusicVolume = ClampVolume(MusicVolume);
	Zone.AmbientVolume = ClampVolume(AmbientVolume);
#else
	(void)ZoneId;
	(void)Priority;
	(void)Weight;
	(void)MasterVolume;
	(void)SFXVolume;
	(void)MusicVolume;
	(void)AmbientVolume;
#endif
}

void FAudioSystem::RemoveZoneMix(uint32 ZoneId)
{
#if NIPS_WITH_MINIAUDIO
	if (ZoneId == 0)
	{
		return;
	}

	Impl->ZoneMixes.erase(ZoneId);
	Impl->EvaluateZoneMixes();
#else
	(void)ZoneId;
#endif
}
