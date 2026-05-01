#pragma once

#include "ActorComponent.h"
#include "Core/CollisionTypes.h"

class ULuaScriptComponent : public UActorComponent
{
public:
	DECLARE_CLASS(ULuaScriptComponent, UActorComponent)

	ULuaScriptComponent();
	~ULuaScriptComponent() override;

	void BeginPlay() override;
	void EndPlay() override;
	void OnRegister() override;
	void OnUnregister() override;
	void PostDuplicate(UObject* Original) override;
	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	bool ReloadScript();
	bool EnsureScriptFile();

	const FString& GetScriptPath() const { return ScriptPath; }
	void SetScriptPath(const FString& InScriptPath);
	bool IsScriptLoaded() const { return bLoaded; }
	bool IsLuaRuntimeEnabled() const;
	const FString& GetLastScriptError() const { return LastScriptError; }

	void HandleBeginOverlap(const FOverlapResult& Overlap);
	void HandleEndOverlap(const FOverlapResult& Overlap);
	void HandleHit(const FHitResult& Hit);

protected:
	void TickComponent(float DeltaTime) override;

private:
	void BindCollisionEvents();
	void UnbindCollisionEvents();
	void EnsureDefaultScriptPath();
	FString MakeDefaultScriptPath() const;
	FString MakeDefaultScriptPathForActor(const AActor* OwnerActor) const;
	void SetLastScriptError(const FString& Error);

	FString ScriptPath;
	FString LastScriptError;
	bool bAutoLoad = true;
	bool bAutoCreateScript = true;
	bool bLoaded = false;
	bool bCollisionEventsBound = false;
	bool bUseDefaultScriptPath = true;
	bool bLoggedRuntimeDisabled = false;
};
