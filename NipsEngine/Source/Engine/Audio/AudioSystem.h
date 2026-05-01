#pragma once

#include "Core/Containers/String.h"
#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Core/Singleton.h"

#include <memory>

struct FAudioHandle
{
	uint32 Id = 0;

	bool IsValid() const { return Id != 0; }
};

struct FAudioPlayParams
{
	bool bLoop = false;
	bool bSpatial = false;
	float Volume = 1.0f;
	float MinDistance = 1.0f;
	float MaxDistance = 20.0f;
	FVector Location = FVector::ZeroVector;
};

struct FAudioSystemImpl;

class FAudioSystem : public TSingleton<FAudioSystem>
{
	friend class TSingleton<FAudioSystem>;

public:
	bool Init();
	void Shutdown();
	void Tick(float DeltaTime);

	bool IsInitialized() const;

	FAudioHandle Play2D(const FString& SoundPath, float Volume = 1.0f, bool bLoop = false);
	FAudioHandle PlayAtLocation(const FString& SoundPath, const FVector& Location, float Volume = 1.0f,
		bool bLoop = false, float MinDistance = 1.0f, float MaxDistance = 20.0f);
	FAudioHandle Play(const FString& SoundPath, const FAudioPlayParams& Params);

	void Stop(FAudioHandle Handle);
	void StopAll();
	bool IsPlaying(FAudioHandle Handle) const;

	void SetSoundPosition(FAudioHandle Handle, const FVector& Location);
	void SetListenerTransform(const FVector& Location, const FVector& Forward, const FVector& Up);

private:
	FAudioSystem();
	~FAudioSystem();

	std::unique_ptr<FAudioSystemImpl> Impl;
};
