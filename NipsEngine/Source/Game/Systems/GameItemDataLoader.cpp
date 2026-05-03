#include "Game/Systems/GameItemDataLoader.h"

#include "Game/Systems/CleaningToolSystem.h"
#include "Game/Systems/ItemSystem.h"
#include "Engine/Core/Logger.h"
#include "Engine/Core/Paths.h"
#include "SimpleJSON/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
	FString GetStringField(const json::JSON& Node, const char* Key)
	{
		if (!Node.hasKey(Key))
		{
			return "";
		}

		return Node.at(Key).ToString();
	}

	bool GetBoolField(const json::JSON& Node, const char* Key, bool DefaultValue)
	{
		if (!Node.hasKey(Key))
		{
			return DefaultValue;
		}

		bool bOk = false;
		const bool Value = Node.at(Key).ToBool(bOk);
		return bOk ? Value : DefaultValue;
	}

	EGameItemType ParseItemType(const FString& TypeName)
	{
		if (TypeName == "DummyItem")
		{
			return EGameItemType::DummyItem;
		}

		if (TypeName == "CleaningTool")
		{
			return EGameItemType::CleaningTool;
		}

		return EGameItemType::StoryItem;
	}

	TArray<FString> ReadStringArray(const json::JSON& Node, const char* Key)
	{
		TArray<FString> Values;
		if (!Node.hasKey(Key))
		{
			return Values;
		}

		const json::JSON& ArrayNode = Node.at(Key);
		if (ArrayNode.JSONType() != json::JSON::Class::Array)
		{
			return Values;
		}

		for (const json::JSON& ValueNode : ArrayNode.ArrayRange())
		{
			bool bOk = false;
			const FString Value = ValueNode.ToString(bOk);
			if (bOk && !Value.empty())
			{
				Values.push_back(Value);
			}
		}

		return Values;
	}

	FString JoinDescriptionLines(const TArray<FString>& Lines)
	{
		FString Result;
		for (size_t Index = 0; Index < Lines.size(); ++Index)
		{
			if (Index > 0)
			{
				Result += "\n";
			}
			Result += Lines[Index];
		}
		return Result;
	}

	FString ReadFoundDescription(const json::JSON& Node)
	{
		if (Node.hasKey("foundDesc"))
		{
			return Node.at("foundDesc").ToString();
		}

		return JoinDescriptionLines(ReadStringArray(Node, "desc"));
	}

	FGameItemData ReadItemData(const json::JSON& Node)
	{
		FGameItemData ItemData;
		ItemData.ItemId = GetStringField(Node, "id");
		ItemData.DisplayName = GetStringField(Node, "name");
		ItemData.ItemType = ParseItemType(GetStringField(Node, "type"));
		ItemData.DescriptionWhenFound = ReadFoundDescription(Node);
		ItemData.DescriptionWhenKept = GetStringField(Node, "keptDesc");
		ItemData.DescriptionWhenDiscarded = GetStringField(Node, "discardedDesc");
		ItemData.IconPath = GetStringField(Node, "iconPath");
		ItemData.bCanClassify = GetBoolField(Node, "canClassify", true);
		ItemData.bRequiredForSuccessEnding = GetBoolField(Node, "requiredForSuccessEnding", false);
		ItemData.StoryFlags = ReadStringArray(Node, "storyFlags");
		return ItemData;
	}

	FCleaningToolData ReadCleaningToolData(const json::JSON& Node, const FGameItemData& ItemData)
	{
		FCleaningToolData ToolData;
		ToolData.ToolId = ItemData.ItemId;
		ToolData.DisplayName = ItemData.DisplayName;
		ToolData.MeshAssetPath = GetStringField(Node, "meshAssetPath");
		ToolData.AnimationSetId = GetStringField(Node, "animationSetId");
		ToolData.EffectId = GetStringField(Node, "effectId");
		ToolData.InteractionSoundId = GetStringField(Node, "interactionSoundId");
		ToolData.CleaningPower = std::max(0.0f, static_cast<float>(Node.hasKey("cleaningPower") ? Node.at("cleaningPower").ToFloat() : 1.0));
		ToolData.UseBobAmplitude = std::max(0.0f, static_cast<float>(Node.hasKey("useBobAmplitude") ? Node.at("useBobAmplitude").ToFloat() : 0.15));
		ToolData.UseBobSpeed = std::max(0.0f, static_cast<float>(Node.hasKey("useBobSpeed") ? Node.at("useBobSpeed").ToFloat() : 8.0));
		ToolData.UseReturnSpeed = std::max(0.0f, static_cast<float>(Node.hasKey("useReturnSpeed") ? Node.at("useReturnSpeed").ToFloat() : 14.0));
		ToolData.ValidSurfaceTypes = ReadStringArray(Node, "validSurfaceTypes");
		return ToolData;
	}

	const json::JSON* FindItemsArray(const json::JSON& Root)
	{
		if (Root.JSONType() == json::JSON::Class::Array)
		{
			return &Root;
		}

		if (Root.JSONType() == json::JSON::Class::Object && Root.hasKey("items"))
		{
			const json::JSON& ItemsNode = Root.at("items");
			if (ItemsNode.JSONType() == json::JSON::Class::Array)
			{
				return &ItemsNode;
			}
		}

		return nullptr;
	}
}

bool FGameItemDataLoader::LoadFromFile(const FString& RelativePath, FItemSystem& ItemSystem)
{
	const std::wstring AbsolutePath = FPaths::ToAbsolute(FPaths::ToWide(RelativePath));
	std::ifstream File{ std::filesystem::path(AbsolutePath) };
	if (!File.is_open())
	{
		UE_LOG("GameItemDataLoader: failed to open '%s'.", RelativePath.c_str());
		return false;
	}

	std::stringstream Buffer;
	Buffer << File.rdbuf();

	json::JSON Root = json::JSON::Load(Buffer.str());
	const json::JSON* ItemsArray = FindItemsArray(Root);
	if (!ItemsArray)
	{
		UE_LOG("GameItemDataLoader: '%s' does not contain an items array.", RelativePath.c_str());
		return false;
	}

	int32 LoadedCount = 0;
	for (const json::JSON& ItemNode : ItemsArray->ArrayRange())
	{
		if (ItemNode.JSONType() != json::JSON::Class::Object)
		{
			continue;
		}

		FGameItemData ItemData = ReadItemData(ItemNode);
		if (ItemData.ItemId.empty())
		{
			UE_LOG("GameItemDataLoader: skipped item without id in '%s'.", RelativePath.c_str());
			continue;
		}

		ItemSystem.RegisterItemData(ItemData);
		if (ItemData.ItemType == EGameItemType::CleaningTool)
		{
			UE_LOG("[CleaningTool] Loading cleaning tool item: id=%s type=CleaningTool mesh=%s",
				ItemData.ItemId.c_str(),
				GetStringField(ItemNode, "meshAssetPath").c_str());
			FCleaningToolSystem::Get().RegisterToolData(ReadCleaningToolData(ItemNode, ItemData));
		}
		++LoadedCount;
	}

	UE_LOG("GameItemDataLoader: loaded %d items from '%s'.", LoadedCount, RelativePath.c_str());
	return LoadedCount > 0;
}
