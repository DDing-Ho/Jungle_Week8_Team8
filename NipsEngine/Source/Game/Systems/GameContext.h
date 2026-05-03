#pragma once

#include "Game/Systems/CleaningGameTypes.h"

class GGameContext
{
public:
	static GGameContext& Get();

	void Reset();

	void SetCleanProgress(float InProgress);
	float GetCleanProgress() const { return CleanProgress; }

	void SetCurrentTool(const FString& ToolId);
	const FString& GetCurrentToolId() const { return CurrentToolId; }

	void SetCurrentInspectedItem(const FString& ItemId);
	void ClearCurrentInspectedItem();
	const FString& GetCurrentInspectedItemId() const { return CurrentInspectedItemId; }

	bool MarkItemFound(const FString& ItemId);
	bool MarkItemKept(const FString& ItemId);
	bool MarkItemDiscarded(const FString& ItemId);

	EGameItemDisposition GetItemDisposition(const FString& ItemId) const;
	bool HasFoundItem(const FString& ItemId) const;
	bool HasKeptItem(const FString& ItemId) const;
	bool HasDiscardedItem(const FString& ItemId) const;

	const TSet<FString>& GetFoundItemIds() const { return FoundItemIds; }
	const TSet<FString>& GetKeptItemIds() const { return KeptItemIds; }
	const TSet<FString>& GetDiscardedItemIds() const { return DiscardedItemIds; }
	size_t GetResolvedItemCount() const { return KeptItemIds.size() + DiscardedItemIds.size(); }

	void UnlockStoryFlag(const FString& Flag);
	bool HasStoryFlag(const FString& Flag) const;
	const TSet<FString>& GetUnlockedStoryFlags() const { return UnlockedStoryFlags; }

	DECLARE_DELEGATE(FOnGameContextChanged);
	DECLARE_DELEGATE(FOnItemDispositionChanged, const FString&, EGameItemDisposition);

	FOnGameContextChanged OnContextChanged;
	FOnItemDispositionChanged OnItemDispositionChanged;

private:
	GGameContext() = default;

	void BroadcastChanged();

	float CleanProgress = 0.0f;
	FString CurrentToolId;
	FString CurrentInspectedItemId;

	TSet<FString> FoundItemIds;
	TSet<FString> KeptItemIds;
	TSet<FString> DiscardedItemIds;
	TSet<FString> UnlockedStoryFlags;
};
