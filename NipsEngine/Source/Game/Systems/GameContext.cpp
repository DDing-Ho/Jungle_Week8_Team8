#include "Game/Systems/GameContext.h"

#include <algorithm>

GGameContext& GGameContext::Get()
{
	static GGameContext Instance;
	return Instance;
}

void GGameContext::Reset()
{
	CleanProgress = 0.0f;
	CurrentToolId.clear();
	CurrentInspectedItemId.clear();
	FoundItemIds.clear();
	KeptItemIds.clear();
	DiscardedItemIds.clear();
	UnlockedStoryFlags.clear();

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
