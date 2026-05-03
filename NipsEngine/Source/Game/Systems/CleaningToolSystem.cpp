#include "Game/Systems/CleaningToolSystem.h"

#include "Game/Systems/GameContext.h"

FCleaningToolSystem& FCleaningToolSystem::Get()
{
	static FCleaningToolSystem Instance;
	return Instance;
}

void FCleaningToolSystem::ClearToolData()
{
	Tools.clear();
}

void FCleaningToolSystem::RegisterToolData(const FCleaningToolData& ToolData)
{
	if (ToolData.ToolId.empty())
	{
		return;
	}

	for (FCleaningToolData& ExistingTool : Tools)
	{
		if (ExistingTool.ToolId == ToolData.ToolId)
		{
			ExistingTool = ToolData;
			return;
		}
	}

	Tools.push_back(ToolData);
}

const FCleaningToolData* FCleaningToolSystem::FindToolData(const FString& ToolId) const
{
	for (const FCleaningToolData& Tool : Tools)
	{
		if (Tool.ToolId == ToolId)
		{
			return &Tool;
		}
	}

	return nullptr;
}

bool FCleaningToolSystem::SelectTool(const FString& ToolId)
{
	if (!FindToolData(ToolId))
	{
		return false;
	}

	GGameContext::Get().SetCurrentTool(ToolId);
	return true;
}

FCleaningToolUseResult FCleaningToolSystem::BuildUseResult(const FString& SurfaceType) const
{
	FCleaningToolUseResult Result;
	const FCleaningToolData* ToolData = FindToolData(GGameContext::Get().GetCurrentToolId());
	if (!ToolData)
	{
		Result.bCanCleanSurface = false;
		return Result;
	}

	Result.ToolId = ToolData->ToolId;
	Result.AnimationSetId = ToolData->AnimationSetId;
	Result.EffectId = ToolData->EffectId;
	Result.InteractionSoundId = ToolData->InteractionSoundId;
	Result.CleaningPower = ToolData->CleaningPower;
	Result.bCanCleanSurface = CanCleanSurface(*ToolData, SurfaceType);
	return Result;
}

bool FCleaningToolSystem::CanCleanSurface(const FCleaningToolData& ToolData, const FString& SurfaceType) const
{
	if (SurfaceType.empty() || ToolData.ValidSurfaceTypes.empty())
	{
		return true;
	}

	for (const FString& ValidSurfaceType : ToolData.ValidSurfaceTypes)
	{
		if (ValidSurfaceType == SurfaceType)
		{
			return true;
		}
	}

	return false;
}
