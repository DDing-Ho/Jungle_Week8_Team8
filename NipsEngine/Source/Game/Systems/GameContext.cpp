#include "Game/Systems/GameContext.h"

#include "Audio/AudioSystem.h"
#include "Component/DecalComponent.h"
#include "Component/Physics/RigidBodyComponent.h"
#include "Engine/GameFramework/AActor.h"
#include "Engine/GameFramework/World.h"
#include "Game/Systems/ItemSystem.h"
#include "Object/Object.h"

#include <algorithm>

namespace
{
	constexpr float CleanDecalThreshold = 0.85f;

	bool IsLiveObjectPointer(const UObject* Object)
	{
		if (Object == nullptr)
		{
			return false;
		}

		for (const UObject* LiveObject : GUObjectArray)
		{
			if (LiveObject == Object)
			{
				return true;
			}
		}

		return false;
	}

	FString ResolveAudioPathFromSoundId(const FString& SoundId)
	{
		if (SoundId.empty())
		{
			return "";
		}

		if (SoundId.find('/') != FString::npos || SoundId.find('\\') != FString::npos || SoundId.find('.') != FString::npos)
		{
			return SoundId;
		}

		return "Asset/Audio/" + SoundId + ".wav";
	}

	void PlayItemSoundId(const FString& SoundId)
	{
		const FString SoundPath = ResolveAudioPathFromSoundId(SoundId);
		if (SoundPath.empty())
		{
			return;
		}

		FAudioPlayParams Params;
		Params.bSpatial = false;
		Params.bLoop = false;
		Params.bAffectedByAudioZones = false;
		Params.Bus = EAudioBus::SFX;
		Params.Volume = 1.0f;
		FAudioSystem::Get().Play(SoundPath, Params);
	}

	const FGameItemData* FindHeldItemData(const FHeldObjectInfo& Info)
	{
		if (!Info.ItemId.empty())
		{
			return FItemSystem::Get().FindItemData(Info.ItemId);
		}

		if (!Info.ToolId.empty())
		{
			return FItemSystem::Get().FindItemData(Info.ToolId);
		}

		return nullptr;
	}

	URigidBodyComponent* FindRigidBodyComponent(AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (URigidBodyComponent* Body = Cast<URigidBodyComponent>(Component))
			{
				return Body;
			}
		}

		return nullptr;
	}

	void QueueItemDropSound(const FHeldObjectInfo& Info)
	{
		const FGameItemData* ItemData = FindHeldItemData(Info);
		if (ItemData == nullptr || ItemData->DropSoundId.empty())
		{
			return;
		}

		if (URigidBodyComponent* Body = FindRigidBodyComponent(Info.Actor))
		{
			Body->QueueDropSound(ResolveAudioPathFromSoundId(ItemData->DropSoundId));
		}
	}
}

GGameContext& GGameContext::Get()
{
	static GGameContext Instance;
	return Instance;
}

void GGameContext::Reset()
{
	const FHeldObjectInfo PreviousHeldObjectInfo = HeldObjectInfo;

	CleanProgress = 0.0f;
	ClearMapDecals();
	CurrentToolId.clear();
	CurrentInspectedItemId.clear();
	HeldObjectInfo = {};
	FoundItemIds.clear();
	KeptItemIds.clear();
	DiscardedItemIds.clear();
	UnlockedStoryFlags.clear();

	if (PreviousHeldObjectInfo.IsHolding())
	{
		OnObjectDropped.Broadcast(PreviousHeldObjectInfo);
		OnHeldObjectChanged.Broadcast(HeldObjectInfo);
	}

	BroadcastChanged();
}

void GGameContext::SetCleanProgress(float InProgress)
{
	const float ClampedProgress = std::clamp(InProgress, 0.0f, 1.0f);
	if (CleanProgress == ClampedProgress)
	{
		return;
	}

	CleanProgress = ClampedProgress;
	BroadcastChanged();
}

void GGameContext::RegisterMapDecals(UWorld* World)
{
	ClearMapDecals();

	if (!World)
	{
		SetCleanProgress(1.0f);
		return;
	}

	for (AActor* Actor : World->GetActors())
	{
		if (!Actor)
		{
			continue;
		}

		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (UDecalComponent* Decal = Cast<UDecalComponent>(Component))
			{
				if (std::find(MapDecals.begin(), MapDecals.end(), Decal) == MapDecals.end())
				{
					MapDecals.push_back(Decal);
				}
			}
		}
	}

	InitialDecalCount = static_cast<int32>(MapDecals.size());
	RefreshCleanProgressFromDecals();
}

void GGameContext::ClearMapDecals()
{
	MapDecals.clear();
	InitialDecalCount = 0;
}

void GGameContext::RefreshCleanProgressFromDecals()
{
	if (InitialDecalCount <= 0)
	{
		SetCleanProgress(1.0f);
		return;
	}

	const int32 RemainingDecalCount = GetRemainingDecalCount();
	const float CleanedRatio = 1.0f - (static_cast<float>(RemainingDecalCount) / static_cast<float>(InitialDecalCount));
	SetCleanProgress(CleanedRatio);
}

int32 GGameContext::GetRemainingDecalCount() const
{
	int32 RemainingCount = 0;

	for (const UDecalComponent* Decal : MapDecals)
	{
		if (!IsLiveObjectPointer(Decal))
		{
			continue;
		}

		const AActor* DecalOwner = Decal->GetOwner();
		if (DecalOwner == nullptr || !DecalOwner->IsActive())
		{
			continue;
		}

		if (Decal->GetCleanPercentage() <= CleanDecalThreshold)
		{
			++RemainingCount;
		}
	}

	return RemainingCount;
}

void GGameContext::SetCurrentTool(const FString& ToolId)
{
	if (CurrentToolId == ToolId)
	{
		return;
	}

	CurrentToolId = ToolId;
	BroadcastChanged();
}

void GGameContext::SetCurrentInspectedItem(const FString& ItemId)
{
	if (CurrentInspectedItemId == ItemId)
	{
		return;
	}

	CurrentInspectedItemId = ItemId;
	BroadcastChanged();
}

void GGameContext::ClearCurrentInspectedItem()
{
	SetCurrentInspectedItem("");
}

void GGameContext::SetHeldObject(AActor* Actor, const FString& ItemId, const FString& ToolId)
{
	FHeldObjectInfo NewInfo;
	NewInfo.Actor = Actor;
	if (Actor != nullptr)
	{
		NewInfo.ActorName = Actor->GetFName().ToString();
	}
	NewInfo.ItemId = ItemId;
	NewInfo.ToolId = ToolId;

	if (!ToolId.empty())
	{
		NewInfo.ObjectType = EGameHeldObjectType::CleaningTool;
	}
	else if (!ItemId.empty())
	{
		NewInfo.ObjectType = EGameHeldObjectType::Item;
	}
	else if (Actor != nullptr)
	{
		NewInfo.ObjectType = EGameHeldObjectType::Object;
	}

	const bool bChanged =
		HeldObjectInfo.Actor != NewInfo.Actor ||
		HeldObjectInfo.ItemId != NewInfo.ItemId ||
		HeldObjectInfo.ToolId != NewInfo.ToolId ||
		HeldObjectInfo.ObjectType != NewInfo.ObjectType;
	if (!bChanged)
	{
		return;
	}

	HeldObjectInfo = NewInfo;
	if (const FGameItemData* ItemData = FindHeldItemData(HeldObjectInfo))
	{
		if (!ItemData->FirstFoundSoundId.empty() && !ItemData->ItemId.empty() && FItemSystem::Get().DiscoverItem(ItemData->ItemId))
		{
			PlayItemSoundId(ItemData->FirstFoundSoundId);
		}
		PlayItemSoundId(ItemData->PickSoundId);
	}
	OnObjectPickedUp.Broadcast(HeldObjectInfo);
	OnHeldObjectChanged.Broadcast(HeldObjectInfo);
	BroadcastChanged();
}

void GGameContext::ClearHeldObject()
{
	if (!HeldObjectInfo.IsHolding())
	{
		return;
	}

	const FHeldObjectInfo PreviousInfo = HeldObjectInfo;
	HeldObjectInfo = {};
	QueueItemDropSound(PreviousInfo);
	OnObjectDropped.Broadcast(PreviousInfo);
	OnHeldObjectChanged.Broadcast(HeldObjectInfo);
	BroadcastChanged();
}

bool GGameContext::MarkItemFound(const FString& ItemId)
{
	if (ItemId.empty())
	{
		return false;
	}

	const bool bInserted = FoundItemIds.insert(ItemId).second;
	if (bInserted)
	{
		OnItemDispositionChanged.Broadcast(ItemId, EGameItemDisposition::Found);
		BroadcastChanged();
	}

	return bInserted;
}

bool GGameContext::MarkItemKept(const FString& ItemId)
{
	if (ItemId.empty())
	{
		return false;
	}

	FoundItemIds.insert(ItemId);
	DiscardedItemIds.erase(ItemId);
	const bool bInserted = KeptItemIds.insert(ItemId).second;
	if (bInserted)
	{
		OnItemDispositionChanged.Broadcast(ItemId, EGameItemDisposition::Kept);
		BroadcastChanged();
	}

	return bInserted;
}

bool GGameContext::MarkItemDiscarded(const FString& ItemId)
{
	if (ItemId.empty())
	{
		return false;
	}

	FoundItemIds.insert(ItemId);
	KeptItemIds.erase(ItemId);
	const bool bInserted = DiscardedItemIds.insert(ItemId).second;
	if (bInserted)
	{
		OnItemDispositionChanged.Broadcast(ItemId, EGameItemDisposition::Discarded);
		BroadcastChanged();
	}

	return bInserted;
}

EGameItemDisposition GGameContext::GetItemDisposition(const FString& ItemId) const
{
	if (HasKeptItem(ItemId))
	{
		return EGameItemDisposition::Kept;
	}

	if (HasDiscardedItem(ItemId))
	{
		return EGameItemDisposition::Discarded;
	}

	if (HasFoundItem(ItemId))
	{
		return EGameItemDisposition::Found;
	}

	return EGameItemDisposition::None;
}

bool GGameContext::HasFoundItem(const FString& ItemId) const
{
	return FoundItemIds.find(ItemId) != FoundItemIds.end();
}

bool GGameContext::HasKeptItem(const FString& ItemId) const
{
	return KeptItemIds.find(ItemId) != KeptItemIds.end();
}

bool GGameContext::HasDiscardedItem(const FString& ItemId) const
{
	return DiscardedItemIds.find(ItemId) != DiscardedItemIds.end();
}

void GGameContext::UnlockStoryFlag(const FString& Flag)
{
	if (Flag.empty())
	{
		return;
	}

	if (UnlockedStoryFlags.insert(Flag).second)
	{
		BroadcastChanged();
	}
}

bool GGameContext::HasStoryFlag(const FString& Flag) const
{
	return UnlockedStoryFlags.find(Flag) != UnlockedStoryFlags.end();
}

void GGameContext::BroadcastChanged()
{
	OnContextChanged.Broadcast();
}
