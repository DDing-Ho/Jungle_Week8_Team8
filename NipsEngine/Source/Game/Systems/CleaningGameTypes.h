#pragma once

#include "Engine/Core/CoreMinimal.h"

enum class EGameItemDisposition
{
	None,
	Found,
	Kept,
	Discarded,
};

enum class EItemDecisionBoxType
{
	KeepBox,
	DiscardBox,
};

enum class EGameItemType
{
	StoryItem,
	DummyItem,
	CleaningTool,
};

struct FGameItemData
{
	FString ItemId;
	FString DisplayName;
	EGameItemType ItemType = EGameItemType::StoryItem;
	FString DescriptionWhenFound;
	FString DescriptionWhenKept;
	FString DescriptionWhenDiscarded;
	FString IconPath;
	bool bCanClassify = true;
	bool bRequiredForSuccessEnding = false;
	TArray<FString> StoryFlags;
};

struct FCleaningToolData
{
	FString ToolId;
	FString DisplayName;
	FString AnimationSetId;
	FString EffectId;
	float CleaningPower = 1.0f;
	TArray<FString> ValidSurfaceTypes;
	FString InteractionSoundId;
};

struct FCleaningToolUseResult
{
	FString ToolId;
	FString AnimationSetId;
	FString EffectId;
	FString InteractionSoundId;
	float CleaningPower = 0.0f;
	bool bCanCleanSurface = true;
};

struct FEndingResult
{
	FString EndingId;
	bool bIsSuccess = false;
	TArray<FString> MissingRequiredItemIds;
	TArray<FString> KeptFailureItemIds;
};
