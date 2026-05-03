#include "Scripting/LuaBindings.h"

#if WITH_LUA
#include "GameFramework/AActor.h"
#include "Component/PrimitiveComponent.h"
#include "Component/DecalComponent.h"
#include "Math/Vector.h"
#include "Math/Vector2.h"
#include "Object/Object.h"
#include "Core/CollisionTypes.h"
#include "Core/Logger.h"
#include "Audio/AudioSystem.h"
#include "Engine/Input/InputRouter.h"
#include "Game/UI/GameUISystem.h"
#include "Game/Systems/GameContext.h"
#include "Game/Systems/ItemSystem.h"

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
		if      (StateName == "None")      GameUISystem::Get().SetState(EGameUIState::None);
		else if (StateName == "StartMenu") GameUISystem::Get().SetState(EGameUIState::StartMenu);
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

	// 아이템 상호작용
	Lua.set_function("PlaceItemInKeepBox", [](const std::string& ItemId)
	{
		return FItemSystem::Get().PlaceItemInDecisionBox(ItemId, EItemDecisionBoxType::KeepBox);
	});

	Lua.set_function("PlaceItemInDiscardBox", [](const std::string& ItemId)
	{
		return FItemSystem::Get().PlaceItemInDecisionBox(ItemId, EItemDecisionBoxType::DiscardBox);
	});

	Lua.set_function("ClassifyItem", [](const std::string& ItemId, const std::string& Disposition)
	{
		if (Disposition == "Kept")
		{
			return FItemSystem::Get().ClassifyItem(ItemId, EGameItemDisposition::Kept);
		}

		if (Disposition == "Discarded")
		{
			return FItemSystem::Get().ClassifyItem(ItemId, EGameItemDisposition::Discarded);
		}

		return false;
	});

	Lua.set_function("GetItemDisplayName", [](const std::string& ItemId)
	{
		const FGameItemData* ItemData = FItemSystem::Get().FindItemData(ItemId);
		return ItemData ? ItemData->DisplayName : FString();
	});

	Lua.set_function("GetItemDescription", [](const std::string& ItemId)
	{
		return FItemSystem::Get().GetDescriptionForCurrentState(ItemId);
	});

	Lua.set_function("GetResolvedItemCount", []()
	{
		return static_cast<int32>(GGameContext::Get().GetResolvedItemCount());
	});


	// 키 입력 (Windows Virtual Key Code)
	// 자주 쓰는 상수를 Lua 전역으로 노출
	Lua.set("KEY_SPACE",  0x20);
	Lua.set("KEY_ESCAPE", 0x1B);
	Lua.set("KEY_P",      0x50);
	Lua.set("KEY_TAB",    0x09);
	Lua.set("KEY_ENTER",  0x0D);

	// 마우스 입력
    Lua.set("KEY_LEFT_MOUSE", 0x01);
    Lua.set("KEY_RIGHT_MOUSE", 0x02);

	Lua.set_function("GetKeyDown", [](int VK)
	{
		if (GameUISystem::Get().WantsMouseCursor()) return false;
		return FInputRouter::GetKeyDown(VK);
	});

	Lua.set_function("GetKey", [](int VK)
	{
		if (GameUISystem::Get().WantsMouseCursor()) return false;
		return FInputRouter::GetKey(VK);
	});

	Lua.set_function("GetKeyUp", [](int VK)
	{
		if (GameUISystem::Get().WantsMouseCursor()) return false;
		return FInputRouter::GetKeyUp(VK);
	});

	Lua.new_usertype<FHitResult>(
		"FHitResult",
		"HitComponent", &FHitResult::HitComponent,
		"Distance", &FHitResult::Distance,
		"Location", &FHitResult::Location,
		"Normal", &FHitResult::Normal,
		"FaceIndex", &FHitResult::FaceIndex,
		"bHit", &FHitResult::bHit,
		"IsValid", &FHitResult::IsValid,
		"GetDecalComponent", [](FHitResult& Hit) -> UDecalComponent*
		{
        if (!Hit.bHit || !Hit.HitComponent) return nullptr;

        // 1. 직접 맞은 게 데칼이면 바로 반환
        if (auto Decal = Cast<UDecalComponent>(Hit.HitComponent))
            return Decal;

        // 2. 맞은 컴포넌트의 액터를 가져옴
        AActor* Owner = Hit.HitComponent->GetOwner();
        if (!Owner) return nullptr;

        // 3. 액터가 가진 모든 컴포넌트를 순회하며 데칼을 찾음
        // 엔진 내부의 컴포넌트 리스트 접근 방식(예: Owner->GetComponents())에 따라 수정하세요.
        for (auto* Comp : Owner->GetComponents()) 
        {
            if (auto* DecalComp = Cast<UDecalComponent>(Comp))
            {
                return DecalComp;
            }
        }

        return nullptr;		}
	);

	Lua.new_usertype<UDecalComponent>(
		"UDecalComponent",
		"GetCleanPercentage", &UDecalComponent::GetCleanPercentage,
		"PaintAtWorldPos", [](UDecalComponent& Decal, const FVector& WorldPos, float Radius, int Value)
		{
			FVector2 UV;
			if (Decal.WorldPosToDecalUV(WorldPos, UV))
				Decal.PaintMask(UV, Radius, static_cast<uint8>(Value));
		}
	);
}
#endif
