#include "Scripting/LuaScriptSystem.h"

#include "Component/LuaScriptComponent.h"
#include "GameFramework/AActor.h"
#include "Scripting/LuaBindings.h"
#include "UI/EditorConsoleWidget.h"

FLuaScriptSystem::FLuaScriptSystem()
{
	bLuaEnabled = WITH_LUA != 0;
}

bool FLuaScriptSystem::LoadScript(ULuaScriptComponent* Component, const FString& ScriptPath)
{
	if (!Component)
	{
		SetLastError("LuaScriptSystem: invalid component.");
		return false;
	}

#if WITH_LUA
	FScriptState State;
	State.Lua = std::make_unique<sol::state>();
	State.ScriptPath = ScriptPath;

	State.Lua->open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::coroutine);
	RegisterLuaBindings(*State.Lua);

	sol::protected_function_result Result = State.Lua->safe_script_file(ScriptPath, sol::script_pass_on_error);
	if (!Result.valid())
	{
		sol::error Error = Result;
		SetLastError(Error.what());
		UE_LOG("LuaScriptSystem: failed to load '%s': %s", ScriptPath.c_str(), LastError.c_str());
		return false;
	}

	Scripts[Component] = std::move(State);
	SetLastError("");
	return true;
#else
	(void)ScriptPath;
	SetLastError("Lua runtime is disabled. Add Lua/sol2 and build with WITH_LUA=1.");
	return false;
#endif
}

bool FLuaScriptSystem::ReloadScript(ULuaScriptComponent* Component, const FString& ScriptPath)
{
	return LoadScript(Component, ScriptPath);
}

void FLuaScriptSystem::UnloadScript(ULuaScriptComponent* Component)
{
#if WITH_LUA
	Scripts.erase(Component);
#else
	(void)Component;
#endif
}

void FLuaScriptSystem::CallBeginPlay(ULuaScriptComponent* Component, AActor* Owner)
{
#if WITH_LUA
	CallFunction(Component, "BeginPlay", Owner);
#else
	(void)Component;
	(void)Owner;
#endif
}

void FLuaScriptSystem::CallTick(ULuaScriptComponent* Component, AActor* Owner, float DeltaTime)
{
#if WITH_LUA
	CallFunction(Component, "Tick", Owner, DeltaTime);
#else
	(void)Component;
	(void)Owner;
	(void)DeltaTime;
#endif
}

void FLuaScriptSystem::CallEndPlay(ULuaScriptComponent* Component, AActor* Owner)
{
#if WITH_LUA
	CallFunction(Component, "EndPlay", Owner);
#else
	(void)Component;
	(void)Owner;
#endif
}

void FLuaScriptSystem::CallOverlap(ULuaScriptComponent* Component, AActor* Owner, const FOverlapResult& Overlap)
{
#if WITH_LUA
	CallFunction(Component, "OnOverlap", Owner, Overlap.OtherActor);
#else
	(void)Component;
	(void)Owner;
	(void)Overlap;
#endif
}

void FLuaScriptSystem::CallEndOverlap(ULuaScriptComponent* Component, AActor* Owner, const FOverlapResult& Overlap)
{
#if WITH_LUA
	CallFunction(Component, "OnEndOverlap", Owner, Overlap.OtherActor);
#else
	(void)Component;
	(void)Owner;
	(void)Overlap;
#endif
}

void FLuaScriptSystem::CallHit(ULuaScriptComponent* Component, AActor* Owner, const FHitResult& Hit)
{
#if WITH_LUA
	CallHitFunction(Component, "OnHit", Owner, Hit);
#else
	(void)Component;
	(void)Owner;
	(void)Hit;
#endif
}

#if WITH_LUA
FLuaScriptSystem::FScriptState* FLuaScriptSystem::FindScript(ULuaScriptComponent* Component)
{
	auto It = Scripts.find(Component);
	return It != Scripts.end() ? &It->second : nullptr;
}

bool FLuaScriptSystem::CallFunction(ULuaScriptComponent* Component, const char* FunctionName)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function();
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

bool FLuaScriptSystem::CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(Owner);
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

bool FLuaScriptSystem::CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, float DeltaTime)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(Owner, DeltaTime);
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

bool FLuaScriptSystem::CallFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, AActor* OtherActor)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(Owner, OtherActor);
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

bool FLuaScriptSystem::CallHitFunction(ULuaScriptComponent* Component, const char* FunctionName, AActor* Owner, const FHitResult& Hit)
{
	FScriptState* State = FindScript(Component);
	if (!State || !State->Lua)
	{
		return false;
	}

	sol::protected_function Function = (*State->Lua)[FunctionName];
	if (!Function.valid())
	{
		return false;
	}

	sol::protected_function_result Result = Function(Owner, Hit);
	if (!Result.valid())
	{
		sol::error Error = Result;
		ReportCallError(Component, FunctionName, Error.what());
		return false;
	}

	return true;
}

void FLuaScriptSystem::ReportCallError(ULuaScriptComponent* Component, const char* FunctionName, const char* ErrorMessage)
{
	const FString ErrorText = ErrorMessage ? ErrorMessage : "unknown Lua error";
	SetLastError(ErrorText);

	const FString ScriptPath = Component ? Component->GetScriptPath() : "";
	if (!ScriptPath.empty())
	{
		UE_LOG("LuaScriptSystem: failed to call %s in '%s': %s", FunctionName, ScriptPath.c_str(), ErrorText.c_str());
	}
	else
	{
		UE_LOG("LuaScriptSystem: failed to call %s: %s", FunctionName, ErrorText.c_str());
	}
}
#endif

void FLuaScriptSystem::SetLastError(const FString& Error)
{
	LastError = Error;
}
