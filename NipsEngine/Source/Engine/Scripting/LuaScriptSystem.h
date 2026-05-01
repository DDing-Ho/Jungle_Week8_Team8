#pragma once

#include "Core/CoreMinimal.h"
#include "Core/CollisionTypes.h"
#include "Core/Singleton.h"

#ifndef WITH_LUA
#define WITH_LUA 0
#endif

#if WITH_LUA
#include <memory>
#include <sol/sol.hpp>
#endif

class AActor;
class ULuaScriptComponent;

class FLuaScriptSystem : public TSingleton<FLuaScriptSystem>
{
	friend class TSingleton<FLuaScriptSystem>;

public:
	bool LoadScript(ULuaScriptComponent* Component, const FString& ScriptPath);
	bool ReloadScript(ULuaScriptComponent* Component, const FString& ScriptPath);
	void UnloadScript(ULuaScriptComponent* Component);

	void CallBeginPlay(ULuaScriptComponent* Component, AActor* Owner);
	void CallTick(ULuaScriptComponent* Component, AActor* Owner, float DeltaTime);
	void CallEndPlay(ULuaScriptComponent* Component, AActor* Owner);
	void CallOverlap(ULuaScriptComponent* Component, AActor* Owner, const FOverlapResult& Overlap);
	void CallEndOverlap(ULuaScriptComponent* Component, AActor* Owner, const FOverlapResult& Overlap);
	void CallHit(ULuaScriptComponent* Component, AActor* Owner, const FHitResult& Hit);

	bool IsLuaEnabled() const { return bLuaEnabled; }
	const FString& GetLastError() const { return LastError; }

private:
	FLuaScriptSystem();

#if WITH_LUA
	struct FScriptState
	{
		std::unique_ptr<sol::state> Lua;
		FString ScriptPath;
	};

	FScriptState* FindScript(ULuaScriptComponent* Component);
	bool CallFunction(ULuaScriptComponent* Component, const char* FunctionName);
	bool CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner);
	bool CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, float DeltaTime);
	bool CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, AActor* OtherActor);
	bool CallHitFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, const FHitResult& Hit);
	void ReportCallError(ULuaScriptComponent* Component, const char* FunctionName, const char* ErrorMessage);

	TMap<ULuaScriptComponent*, FScriptState> Scripts;
#endif

	void SetLastError(const FString& Error);

	bool bLuaEnabled = false;
	FString LastError;
};
