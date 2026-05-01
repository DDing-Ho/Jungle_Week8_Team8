#include "Scripting/LuaScriptSystem.h"

#include "Component/LuaScriptComponent.h"
#include "GameFramework/AActor.h"
#include "Scripting/LuaBindings.h"
#include "UI/EditorConsoleWidget.h"

#include <algorithm>
#include <string>
#include <utility>

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
	BindCoroutineAPI(Component, State);

	sol::protected_function_result Result = State.Lua->safe_script_file(ScriptPath, sol::script_pass_on_error);
	if (!Result.valid())
	{
		sol::error Error = Result;
		SetLastError(Error.what());
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
	UnloadScript(Component);
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
	if (FScriptState* State = FindScript(Component))
	{
		State->CoroutineScheduler.Tick(DeltaTime);
	}
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
void FLuaScriptSystem::BindCoroutineAPI(ULuaScriptComponent* Component, FScriptState& State)
{
	if (!State.Lua)
	{
		return;
	}

	State.Lua->set_function("print", [](sol::variadic_args Args)
	{
		std::string Message;
		for (sol::object Arg : Args)
		{
			if (!Message.empty())
			{
				Message += "\t";
			}

			switch (Arg.get_type())
			{
			case sol::type::nil:
				Message += "nil";
				break;
			case sol::type::boolean:
				Message += Arg.as<bool>() ? "true" : "false";
				break;
			case sol::type::number:
				Message += std::to_string(Arg.as<double>());
				break;
			case sol::type::string:
				Message += Arg.as<std::string>();
				break;
			default:
				Message += "<";
				Message += sol::type_name(Arg.lua_state(), Arg.get_type());
				Message += ">";
				break;
			}
		}

		UE_LOG("[Lua] %s", Message.c_str());
	});

	State.Lua->set_function("wait", sol::yielding([](float Seconds)
	{
		return std::max(0.0f, Seconds);
	}));

	sol::table CoroutineTable = (*State.Lua)["coroutine"];
	State.NativeCoroutineCreate = CoroutineTable["create"];
	State.NativeCoroutineResume = CoroutineTable["resume"];

	CoroutineTable.set_function("create", [this, Component](sol::function Function)
	{
		return CreateCoroutine(Component, Function, true).Id;
	});

	CoroutineTable.set_function("resume", [this, Component](int32 CoroutineId)
	{
		return ResumeCoroutine(Component, FLuaCoroutineHandle{ CoroutineId });
	});

	CoroutineTable.set_function("yield", sol::yielding([](sol::optional<float> Seconds)
	{
		return std::max(0.0f, Seconds.value_or(0.0f));
	}));

	CoroutineTable.set_function("status", [this, Component](int32 CoroutineId)
	{
		FScriptState* ScriptState = FindScript(Component);
		if (ScriptState == nullptr)
		{
			return FString("dead");
		}

		return ScriptState->CoroutineScheduler.IsRunning(FLuaCoroutineHandle{ CoroutineId }) ? FString("suspended") : FString("dead");
	});

	State.Lua->set_function("yield", sol::yielding([](sol::optional<float> Seconds)
	{
		return std::max(0.0f, Seconds.value_or(0.0f));
	}));

	State.Lua->set_function("StartCoroutine", [this, Component](sol::function Function)
	{
		return StartCoroutine(Component, Function).Id;
	});

	State.Lua->set_function("CreateCoroutine", [this, Component](sol::function Function)
	{
		return CreateCoroutine(Component, Function, true).Id;
	});

	State.Lua->set_function("ResumeCoroutine", [this, Component](int32 CoroutineId)
	{
		return ResumeCoroutine(Component, FLuaCoroutineHandle{ CoroutineId });
	});

	State.Lua->set_function("CancelCoroutine", [this, Component](int32 CoroutineId)
	{
		FScriptState* ScriptState = FindScript(Component);
		if (ScriptState == nullptr)
		{
			return false;
		}

		return ScriptState->CoroutineScheduler.Cancel(FLuaCoroutineHandle{ CoroutineId });
	});
}

FLuaCoroutineHandle FLuaScriptSystem::CreateCoroutine(ULuaScriptComponent* Component, sol::function Function, bool bStartPaused)
{
	FScriptState* State = FindScript(Component);
	if (State == nullptr || !State->Lua || !Function.valid())
	{
		return {};
	}

	if (!State->NativeCoroutineCreate.valid() || !State->NativeCoroutineResume.valid())
	{
		UE_LOG("Lua coroutine error: native coroutine API is not available.");
		return {};
	}

	sol::protected_function NativeCreate = State->NativeCoroutineCreate;
	sol::protected_function NativeResume = State->NativeCoroutineResume;
	sol::protected_function_result CreateResult = NativeCreate(Function);
	if (!CreateResult.valid())
	{
		sol::error Error = CreateResult;
		UE_LOG("Lua coroutine create error: %s", Error.what());
		return {};
	}

	sol::object Coroutine = CreateResult.get<sol::object>();

	auto ResumeCallback =
		[NativeResume = std::move(NativeResume), Coroutine = std::move(Coroutine)]() mutable
		{
			sol::protected_function_result Result = NativeResume(Coroutine);
			if (!Result.valid())
			{
				sol::error Error = Result;
				UE_LOG("Lua coroutine error: %s", Error.what());
				return FLuaCoroutineScheduler::Finish();
			}

			const bool bResumeSucceeded = Result.return_count() > 0 && Result.get<bool>(0);
			if (!bResumeSucceeded)
			{
				FString ErrorMessage = "unknown coroutine error";
				if (Result.return_count() > 1)
				{
					ErrorMessage = Result.get<FString>(1);
				}

				UE_LOG("Lua coroutine error: %s", ErrorMessage.c_str());
				return FLuaCoroutineScheduler::Finish();
			}

			if (Result.return_count() > 1)
			{
				const float WaitSeconds = Result.get<float>(1);
				return FLuaCoroutineScheduler::Wait(WaitSeconds);
			}

			return FLuaCoroutineScheduler::Finish();
		};

	return bStartPaused
		? State->CoroutineScheduler.CreatePaused(std::move(ResumeCallback))
		: State->CoroutineScheduler.StartCoroutine(std::move(ResumeCallback));
}

FLuaCoroutineHandle FLuaScriptSystem::StartCoroutine(ULuaScriptComponent* Component, sol::function Function)
{
	FLuaCoroutineHandle Handle = CreateCoroutine(Component, Function, true);
	ResumeCoroutine(Component, Handle);
	return Handle;
}

bool FLuaScriptSystem::ResumeCoroutine(ULuaScriptComponent* Component, FLuaCoroutineHandle Handle)
{
	FScriptState* State = FindScript(Component);
	if (State == nullptr || !Handle.IsValid())
	{
		return false;
	}

	return State->CoroutineScheduler.Resume(Handle);
}

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
		SetLastError(Error.what());
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
		SetLastError(Error.what());
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
		SetLastError(Error.what());
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
		SetLastError(Error.what());
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
		SetLastError(Error.what());
		return false;
	}

	return true;
}
#endif

void FLuaScriptSystem::SetLastError(const FString& Error)
{
	LastError = Error;
}
