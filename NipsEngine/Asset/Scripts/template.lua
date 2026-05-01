-- LuaScriptComponent contract
-- owner: AActor bound from C++
-- otherActor: AActor or nil
-- hit: FHitResult
-- Log(message): writes to the editor console

function BeginPlay(owner)
    Log("[BeginPlay] " .. owner:GetName())
end

function EndPlay(owner)
    Log("[EndPlay] " .. owner:GetName())
end

function OnOverlap(owner, otherActor)
    if otherActor ~= nil then
        Log(owner:GetName() .. " overlapped " .. otherActor:GetName())
    end
end

function OnEndOverlap(owner, otherActor)
end

function OnHit(owner, hit)
end

function Tick(owner, deltaTime)
end
