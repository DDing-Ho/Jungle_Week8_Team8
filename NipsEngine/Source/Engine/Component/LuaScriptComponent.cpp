#include "Component/LuaScriptComponent.h"

#include "Component/PrimitiveComponent.h"
#include "Core/Paths.h"
#include "GameFramework/AActor.h"
#include "Object/ObjectFactory.h"
#include "Scripting/LuaScriptSystem.h"
#include "UI/EditorConsoleWidget.h"

#include <filesystem>
#include <fstream>
#include <cstring>

DEFINE_CLASS(ULuaScriptComponent, UActorComponent)
REGISTER_FACTORY(ULuaScriptComponent)

namespace
{
	FString SanitizeScriptName(FString Name)
	{
		if (Name.empty())
		{
			return "Actor";
		}

		for (char& Ch : Name)
		{
			const bool bAlphaNum = (Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9');
			if (!bAlphaNum && Ch != '_' && Ch != '-')
			{
				Ch = '_';
			}
		}

		return Name;
	}
}

ULuaScriptComponent::ULuaScriptComponent()
{
	bCanEverTick = true;
}

ULuaScriptComponent::~ULuaScriptComponent()
{
	FLuaScriptSystem::Get().UnloadScript(this);
}

void ULuaScriptComponent::BeginPlay()
{
	UActorComponent::BeginPlay();
	BindCollisionEvents();

	EnsureDefaultScriptPath();

	if (bAutoCreateScript)
	{
		EnsureScriptFile();
	}

	if (bAutoLoad)
	{
		ReloadScript();
	}

	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallBeginPlay(this, GetOwner());
	}
}

void ULuaScriptComponent::EndPlay()
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallEndPlay(this, GetOwner());
	}

	FLuaScriptSystem::Get().UnloadScript(this);
	bLoaded = false;
}

void ULuaScriptComponent::OnRegister()
{
	if (bRegistered)
	{
		return;
	}

	bRegistered = true;

	if (GetOwner())
	{
		EnsureDefaultScriptPath();
		BindCollisionEvents();
	}
}

void ULuaScriptComponent::OnUnregister()
{
	UnbindCollisionEvents();
	FLuaScriptSystem::Get().UnloadScript(this);
	bLoaded = false;
	bRegistered = false;
}

void ULuaScriptComponent::PostDuplicate(UObject* Original)
{
	UObject::PostDuplicate(Original);

	const ULuaScriptComponent* OriginalScript = Cast<ULuaScriptComponent>(Original);
	if (!OriginalScript)
	{
		return;
	}

	if (ScriptPath.empty())
	{
		if (!OriginalScript->ScriptPath.empty())
		{
			ScriptPath = OriginalScript->ScriptPath;
		}
		else
		{
			ScriptPath = MakeDefaultScriptPathForActor(OriginalScript->GetOwner());
			bUseDefaultScriptPath = true;
		}
	}

	bLoaded = false;
	bCollisionEventsBound = false;
	bLoggedRuntimeDisabled = false;
	LastScriptError.clear();
}

void ULuaScriptComponent::Serialize(FArchive& Ar)
{
	UActorComponent::Serialize(Ar);
	Ar << "ScriptPath" << ScriptPath;
	Ar << "AutoLoad" << bAutoLoad;
	Ar << "AutoCreateScript" << bAutoCreateScript;
	Ar << "UseDefaultScriptPath" << bUseDefaultScriptPath;

	if (Ar.IsLoading())
	{
		bUseDefaultScriptPath = bUseDefaultScriptPath || ScriptPath.empty();
	}
}

void ULuaScriptComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
	UActorComponent::GetEditableProperties(OutProps);
	OutProps.push_back({ "Use Default Script Path", EPropertyType::Bool, &bUseDefaultScriptPath });
	OutProps.push_back({ "Script Path", EPropertyType::String, &ScriptPath });
	OutProps.push_back({ "Auto Load", EPropertyType::Bool, &bAutoLoad });
	OutProps.push_back({ "Auto Create Script", EPropertyType::Bool, &bAutoCreateScript });
}

void ULuaScriptComponent::PostEditProperty(const char* PropertyName)
{
	UActorComponent::PostEditProperty(PropertyName);
	if (PropertyName && strcmp(PropertyName, "Script Path") == 0)
	{
		bLoaded = false;
		if (ScriptPath.empty())
		{
			bUseDefaultScriptPath = true;
		}
	}
	else if (PropertyName && strcmp(PropertyName, "Use Default Script Path") == 0)
	{
		bLoaded = false;
		if (bUseDefaultScriptPath)
		{
			ScriptPath = MakeDefaultScriptPath();
		}
	}
}

bool ULuaScriptComponent::ReloadScript()
{
	EnsureDefaultScriptPath();

	bLoaded = FLuaScriptSystem::Get().ReloadScript(this, FPaths::ToAbsoluteString(FPaths::ToWide(ScriptPath)));
	SetLastScriptError(FLuaScriptSystem::Get().GetLastError());

	if (!bLoaded && !IsLuaRuntimeEnabled() && !bLoggedRuntimeDisabled)
	{
		UE_LOG("LuaScriptComponent: Lua runtime is disabled. Script '%s' was not loaded.", ScriptPath.c_str());
		bLoggedRuntimeDisabled = true;
	}
	else if (!bLoaded && !LastScriptError.empty())
	{
		UE_LOG("LuaScriptComponent: failed to load '%s': %s", ScriptPath.c_str(), LastScriptError.c_str());
	}

	return bLoaded;
}

bool ULuaScriptComponent::EnsureScriptFile()
{
	EnsureDefaultScriptPath();

	std::filesystem::path Target(FPaths::ToAbsolute(FPaths::ToWide(ScriptPath)));
	if (std::filesystem::exists(Target))
	{
		SetLastScriptError("");
		return true;
	}

	std::error_code ErrorCode;
	std::filesystem::create_directories(Target.parent_path(), ErrorCode);
	if (ErrorCode)
	{
		SetLastScriptError("Failed to create script directory: " + ErrorCode.message());
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	std::filesystem::path Template(FPaths::ToAbsolute(L"Asset/Scripts/template.lua"));
	if (std::filesystem::exists(Template))
	{
		std::filesystem::copy_file(Template, Target, std::filesystem::copy_options::overwrite_existing, ErrorCode);
		if (!ErrorCode)
		{
			SetLastScriptError("");
			return true;
		}
	}

	std::ofstream Out(Target);
	if (!Out.is_open())
	{
		SetLastScriptError("Failed to create script file: " + FPaths::ToUtf8(Target.wstring()));
		UE_LOG("LuaScriptComponent: %s", LastScriptError.c_str());
		return false;
	}

	Out << "function BeginPlay(owner)\nend\n\n";
	Out << "function Tick(owner, deltaTime)\nend\n\n";
	Out << "function EndPlay(owner)\nend\n\n";
	Out << "function OnOverlap(owner, otherActor)\nend\n\n";
	Out << "function OnEndOverlap(owner, otherActor)\nend\n\n";
	Out << "function OnHit(owner, hit)\nend\n";
	SetLastScriptError("");
	return true;
}

void ULuaScriptComponent::HandleBeginOverlap(const FOverlapResult& Overlap)
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallOverlap(this, GetOwner(), Overlap);
	}
}

void ULuaScriptComponent::HandleEndOverlap(const FOverlapResult& Overlap)
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallEndOverlap(this, GetOwner(), Overlap);
	}
}

void ULuaScriptComponent::HandleHit(const FHitResult& Hit)
{
	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallHit(this, GetOwner(), Hit);
	}
}

void ULuaScriptComponent::SetScriptPath(const FString& InScriptPath)
{
	ScriptPath = InScriptPath;
	bLoaded = false;
	bUseDefaultScriptPath = ScriptPath.empty();
	SetLastScriptError("");
	bLoggedRuntimeDisabled = false;
}

bool ULuaScriptComponent::IsLuaRuntimeEnabled() const
{
	return FLuaScriptSystem::Get().IsLuaEnabled();
}

void ULuaScriptComponent::TickComponent(float DeltaTime)
{
	if (!bCollisionEventsBound)
	{
		BindCollisionEvents();
	}

	if (bLoaded)
	{
		FLuaScriptSystem::Get().CallTick(this, GetOwner(), DeltaTime);
	}
}

void ULuaScriptComponent::BindCollisionEvents()
{
	if (bCollisionEventsBound)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	bool bAnyBound = false;
	for (UPrimitiveComponent* Primitive : OwnerActor->GetPrimitiveComponents())
	{
		if (!Primitive)
		{
			continue;
		}

		Primitive->OnComponentBeginOverlap.AddDynamic(this, &ULuaScriptComponent::HandleBeginOverlap);
		Primitive->OnComponentEndOverlap.AddDynamic(this, &ULuaScriptComponent::HandleEndOverlap);
		Primitive->OnComponentHit.AddDynamic(this, &ULuaScriptComponent::HandleHit);
		bAnyBound = true;
	}

	bCollisionEventsBound = bAnyBound;
}

void ULuaScriptComponent::UnbindCollisionEvents()
{
	if (!bCollisionEventsBound)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		bCollisionEventsBound = false;
		return;
	}

	for (UPrimitiveComponent* Primitive : OwnerActor->GetPrimitiveComponents())
	{
		if (!Primitive)
		{
			continue;
		}

		Primitive->OnComponentBeginOverlap.RemoveDynamic(this);
		Primitive->OnComponentEndOverlap.RemoveDynamic(this);
		Primitive->OnComponentHit.RemoveDynamic(this);
	}

	bCollisionEventsBound = false;
}

void ULuaScriptComponent::EnsureDefaultScriptPath()
{
	if (!ScriptPath.empty())
	{
		return;
	}

	ScriptPath = MakeDefaultScriptPath();
	bUseDefaultScriptPath = true;
}

FString ULuaScriptComponent::MakeDefaultScriptPath() const
{
	return MakeDefaultScriptPathForActor(GetOwner());
}

FString ULuaScriptComponent::MakeDefaultScriptPathForActor(const AActor* OwnerActor) const
{
	const FString ActorName = OwnerActor ? OwnerActor->GetFName().ToString() : "Actor";
	return "Asset/Scripts/" + SanitizeScriptName(ActorName) + ".lua";
}

void ULuaScriptComponent::SetLastScriptError(const FString& Error)
{
	LastScriptError = Error;
}
