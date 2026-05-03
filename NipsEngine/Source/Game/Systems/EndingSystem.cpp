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

	const GGameContext& Context = GGameContext::Get();
	const FItemSystem& ItemSystem = FItemSystem::Get();

	for (const FGameItemData& ItemData : ItemSystem.GetAllItemData())
	{
		if (ItemData.bRequiredForSuccessEnding && !Context.HasKeptItem(ItemData.ItemId))
		{
			Result.MissingRequiredItemIds.push_back(ItemData.ItemId);
		}
	}

	for (const FString& ItemId : Context.GetKeptItemIds())
	{
		const FGameItemData* ItemData = ItemSystem.FindItemData(ItemId);
		if (ItemData && ItemData->ItemType == EGameItemType::DummyItem)
		{
			Result.KeptFailureItemIds.push_back(ItemId);
		}
	}

	Result.bIsSuccess = Result.MissingRequiredItemIds.empty() && Result.KeptFailureItemIds.empty();
	if (Result.bIsSuccess)
	{
		Result.EndingId = "Ending_Success";
	}
	else if (!Result.KeptFailureItemIds.empty())
	{
		Result.EndingId = "Ending_Failed_DummyItem";
	}
	else
	{
		Result.EndingId = "Ending_Failed_MissingRequiredItem";
	}

	return Result;
}

FString FEndingSystem::BuildEndingIdFromContext() const
{
	return EvaluateEnding().EndingId;
}
