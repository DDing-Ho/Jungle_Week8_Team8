local ItemIdByActorName = {
    -- ["OldPhotoActor"] = "old_photo",
}

local function GetItemId(otherActor)
    if otherActor == nil then
        return nil
    end

    local actorName = otherActor:GetName()
    return ItemIdByActorName[actorName] or actorName
end

local function RefreshItemUI(itemId)
    local name = GetItemDisplayName(itemId)
    local desc = GetItemDescription(itemId)

    if name ~= nil and name ~= "" then
        SetCurrentItem(name, desc or "")
    end

    SetItemCount(GetResolvedItemCount())
end

function BeginPlay(owner)
    Log(owner:GetName() .. " ready as discard box")
end

function OnOverlap(owner, otherActor)
    local itemId = GetItemId(otherActor)
    if itemId == nil or itemId == "" then
        return
    end

    local classified = PlaceItemInDiscardBox(itemId)
    if classified then
        RefreshItemUI(itemId)
        Log("Discarded item: " .. itemId)
    else
        Log("Discard box ignored: " .. itemId)
    end
end

function OnEndOverlap(owner, otherActor)
end

function Tick(owner, deltaTime)
end
