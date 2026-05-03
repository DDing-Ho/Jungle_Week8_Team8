#include "Game/Systems/EndingSystem.h"

#include "Game/Systems/GameContext.h"
#include "Game/Systems/ItemSystem.h"

FEndingSystem& FEndingSystem::Get()
{
	static FEndingSystem Instance;
	return Instance;
}

FEndingResult FEndingSystem::EvaluateEnding() const
{
	FEndingResult Result;
	Result.EndingId = BuildEndingIdFromContext();

	const GGameContext& Context = GGameContext::Get();
	for (const FString& ItemId : Context.GetKeptItemIds())
	{
		if (const FGameItemData* ItemData = FItemSystem::Get().FindItemData(ItemId))
		{
			Result.MatchedTags.insert(Result.MatchedTags.end(), ItemData->EndingTags.begin(), ItemData->EndingTags.end());
		}
	}

	return Result;
}

FString FEndingSystem::BuildEndingIdFromContext() const
{
	const GGameContext& Context = GGameContext::Get();
	const size_t KeptCount = Context.GetKeptItemIds().size();
	const size_t DiscardedCount = Context.GetDiscardedItemIds().size();

	if (KeptCount == 0 && DiscardedCount == 0)
	{
		return "Ending_Unresolved";
	}

	if (KeptCount >= DiscardedCount)
	{
		return "Ending_Collector";
	}

	return "Ending_Discarder";
}
