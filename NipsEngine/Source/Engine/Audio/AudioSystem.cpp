#include "Audio/AudioSystem.h"

#include "UI/EditorConsoleWidget.h"

#include <memory>

#if __has_include(<miniaudio.h>)
	#define NIPS_WITH_MINIAUDIO 1
	#define MINIAUDIO_IMPLEMENTATION
	#include <miniaudio.h>
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
	float Clamp01(float Value)
	{
		if (Value < 0.0f) return 0.0f;
		if (Value > 1.0f) return 1.0f;
		return Value;
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
	};

	ma_engine Engine{};
	bool bInitialized = false;
	uint32 NextHandleId = 1;
	std::unordered_map<uint32, FActiveSound> ActiveSounds;
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
	const ma_uint32 Flags = MA_SOUND_FLAG_DECODE;
	ma_result Result = ma_sound_init_from_file(&Impl->Engine, AbsolutePath.c_str(), Flags, nullptr, nullptr, Sound.get());
	if (Result != MA_SUCCESS)
	{
		UE_LOG("AudioSystem: failed to load sound '%s'. error=%d", SoundPath.c_str(), static_cast<int>(Result));
		return {};
	}

	ma_sound_set_volume(Sound.get(), Clamp01(Params.Volume));
	ma_sound_set_looping(Sound.get(), Params.bLoop ? MA_TRUE : MA_FALSE);
	ma_sound_set_spatialization_enabled(Sound.get(), Params.bSpatial ? MA_TRUE : MA_FALSE);

	if (Params.bSpatial)
	{
		const float MinDistance = std::max(0.01f, Params.MinDistance);
		const float MaxDistance = std::max(MinDistance, Params.MaxDistance);
		ma_sound_set_position(Sound.get(), Params.Location.X, Params.Location.Y, Params.Location.Z);
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

	Impl->ActiveSounds[Handle.Id] = FAudioSystemImpl::FActiveSound{ std::move(Sound), Params.bLoop };
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

	ma_sound_set_position(It->second.Sound.get(), Location.X, Location.Y, Location.Z);
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

	const FVector SafeForward = Forward.GetSafeNormal();
	const FVector SafeUp = Up.GetSafeNormal();
	ma_engine_listener_set_position(&Impl->Engine, 0, Location.X, Location.Y, Location.Z);
	ma_engine_listener_set_direction(&Impl->Engine, 0, SafeForward.X, SafeForward.Y, SafeForward.Z);
	ma_engine_listener_set_world_up(&Impl->Engine, 0, SafeUp.X, SafeUp.Y, SafeUp.Z);
#else
	(void)Location;
	(void)Forward;
	(void)Up;
#endif
}
