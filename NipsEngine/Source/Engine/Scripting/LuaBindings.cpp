#include "Scripting/LuaBindings.h"

#if WITH_LUA
#include "GameFramework/AActor.h"
#include "Component/PrimitiveComponent.h"
#include "Math/Vector.h"
#include "Object/Object.h"
#include "Core/CollisionTypes.h"
#include "Core/Logger.h"
#include "Audio/AudioSystem.h"
#include "Engine/UI/GameUISystem.h"

void RegisterLuaBindings(sol::state& Lua)
{
	Lua.set_function("Log", [](const FString& Message)
	{
		UE_LOG("[Lua] %s", Message.c_str());
	});

	Lua.set_function("PlaySound", [](const FString& SoundPath)
	{
		return static_cast<int32>(FAudioSystem::Get().Play2D(SoundPath).Id);
	});

	Lua.set_function("PlaySoundAt", [](const FString& SoundPath, const FVector& Location)
	{
		return static_cast<int32>(FAudioSystem::Get().PlayAtLocation(SoundPath, Location).Id);
	});

	Lua.set_function("StopSound", [](int32 HandleId)
	{
		FAudioSystem::Get().Stop(FAudioHandle{ static_cast<uint32>(HandleId) });
	});

	Lua.new_usertype<FVector>(
		"FVector",
		sol::constructors<FVector(), FVector(float, float, float)>(),
		"x", &FVector::X,
		"y", &FVector::Y,
		"z", &FVector::Z,
		"X", &FVector::X,  
		"Y", &FVector::Y,
		"Z", &FVector::Z
	);

	Lua.new_usertype<UObject>(
		"UObject",
		"GetUUID", &UObject::GetUUID,
		"GetName", [](UObject& Object) { return Object.GetFName().ToString(); }
	);

	Lua.new_usertype<AActor>(
		"AActor",
		sol::base_classes, sol::bases<UObject>(),
		"GetActorLocation", &AActor::GetActorLocation,
		"SetActorLocation", &AActor::SetActorLocation,
		"AddActorWorldOffset", &AActor::AddActorWorldOffset,
		"GetActorRotation", &AActor::GetActorRotation,
		"SetActorRotation", &AActor::SetActorRotation,
		"GetActorScale", &AActor::GetActorScale,
		"SetActorScale", &AActor::SetActorScale,
		"IsVisible", &AActor::IsVisible,
		"SetVisible", &AActor::SetVisible,
		"GetName", [](AActor& Actor) { return Actor.GetFName().ToString(); },
		"GetUUID", &AActor::GetUUID
	);

	// -------------------------------------------------------
	// GameUI
	// -------------------------------------------------------
	Lua.set_function("SetUIState", [](const std::string& StateName)
	{
		if      (StateName == "StartMenu") GameUISystem::Get().SetState(EGameUIState::StartMenu);
		else if (StateName == "Prologue")  GameUISystem::Get().SetState(EGameUIState::Prologue);
		else if (StateName == "InGame")    GameUISystem::Get().SetState(EGameUIState::InGame);
		else if (StateName == "Ending")    GameUISystem::Get().SetState(EGameUIState::Ending);
	});

	Lua.set_function("SetProgress", [](float Progress)
	{
		GameUISystem::Get().SetProgress(Progress);
	});

	Lua.set_function("SetCurrentItem", [](const std::string& Name, const std::string& Desc)
	{
		GameUISystem::Get().SetCurrentItem(Name.c_str(), Desc.c_str());
	});

	Lua.set_function("SetItemCount", [](int Count)
	{
		GameUISystem::Get().SetItemCount(Count);
	});

	Lua.set_function("SetElapsedTime", [](float Seconds)
	{
		GameUISystem::Get().SetElapsedTime(Seconds);
	});

	Lua.set_function("SetPauseMenuOpen", [](bool bOpen)
	{
		GameUISystem::Get().SetPauseMenuOpen(bOpen);
	});

	Lua.set_function("IsPauseMenuOpen", []()
	{
		return GameUISystem::Get().IsPauseMenuOpen();
	});

	// -------------------------------------------------------
	// Dialogue
	// -------------------------------------------------------
	Lua.set_function("ShowDialogue", [](const std::string& Speaker, const std::string& Text)
	{
		GameUISystem::Get().ShowDialogue(Speaker.c_str(), Text.c_str());
	});

	Lua.set_function("QueueDialogue", [](const std::string& Speaker, const std::string& Text)
	{
		GameUISystem::Get().QueueDialogue(Speaker.c_str(), Text.c_str());
	});

	Lua.set_function("HideDialogue", []()
	{
		GameUISystem::Get().HideDialogue();
	});

	Lua.set_function("IsDialogueActive", []()
	{
		return GameUISystem::Get().IsDialogueActive();
	});

	// 키 입력 (Windows Virtual Key Code)
	// 자주 쓰는 상수를 Lua 전역으로 노출
	Lua.set("KEY_SPACE",  0x20);
	Lua.set("KEY_ESCAPE", 0x1B);
	Lua.set("KEY_P",      0x50);
	Lua.set("KEY_TAB",    0x09);
	Lua.set("KEY_ENTER",  0x0D);

	Lua.set_function("GetKeyDown", [](int VK)
	{
		return InputSystem::Get().GetKeyDown(VK);
	});

	Lua.set_function("GetKeyUp", [](int VK)
	{
		return InputSystem::Get().GetKeyUp(VK);
	});

	Lua.new_usertype<FHitResult>(
		"FHitResult",
		"HitComponent", &FHitResult::HitComponent,
		"Distance", &FHitResult::Distance,
		"Location", &FHitResult::Location,
		"Normal", &FHitResult::Normal,
		"FaceIndex", &FHitResult::FaceIndex,
		"bHit", &FHitResult::bHit,
		"IsValid", &FHitResult::IsValid
	);
}
#endif
