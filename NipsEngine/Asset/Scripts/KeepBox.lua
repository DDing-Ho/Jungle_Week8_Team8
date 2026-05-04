local ItemIdByActorName = {
    -- ["OldPhotoActor"] = "old_photo",
}

local function GetItemId(otherActor)
    if otherActor == nil then
        return nil
    end

    local actorName = otherActor:GetName()
    local registeredItemId = GetRegisteredItemId(otherActor)
    if registeredItemId ~= nil and registeredItemId ~= "" then
        return registeredItemId
    end

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
    Log(owner:GetName() .. " ready as keep box")
end

local function HandleItemContact(otherActor)
    if otherActor == nil then
        Log("Keep box hit: otherActor=nil")
        return
    end

    local itemId = GetItemId(otherActor)
    if itemId == nil or itemId == "" then
        Log("Keep box hit ignored: actor=" .. otherActor:GetName() .. " itemId=nil")
        return
    end

    Log("Keep box hit: actor=" .. otherActor:GetName() .. " itemId=" .. itemId)

    local classified = PlaceItemInKeepBox(itemId)
    if classified then
        RefreshItemUI(itemId)
        DeactivateActor(otherActor)
        Log("Kept item: " .. itemId)
    else
        Log("Keep box ignored: " .. itemId)
    end
end

function OnOverlap(owner, otherActor)
end

function OnEndOverlap(owner, otherActor)
end

function OnHit(owner, hit)
    if hit == nil or not hit:IsValid() then
        Log("Keep box hit ignored: invalid hit")
        return
    end

    HandleItemContact(hit:GetHitActor())
end

function Tick(owner, deltaTime)
end
