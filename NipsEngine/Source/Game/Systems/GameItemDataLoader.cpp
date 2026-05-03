#include "Game/Systems/GameItemDataLoader.h"

#include "Game/Systems/ItemSystem.h"
#include "Engine/Core/Logger.h"
#include "Engine/Core/Paths.h"
#include "SimpleJSON/json.hpp"

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
		++LoadedCount;
	}

	UE_LOG("GameItemDataLoader: loaded %d items from '%s'.", LoadedCount, RelativePath.c_str());
	return LoadedCount > 0;
}
