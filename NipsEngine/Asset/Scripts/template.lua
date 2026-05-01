function BeginPlay(owner)
    print("[BeginPlay] " .. owner:GetName())
end

function EndPlay(owner)
    print("[EndPlay] " .. owner:GetName())
end

function OnOverlap(owner, otherActor)
    if otherActor ~= nil then
        print(owner:GetName() .. " overlapped " .. otherActor:GetName())
    end
end

function OnEndOverlap(owner, otherActor)
end

function OnHit(owner, hit)
end

function Tick(owner, deltaTime)
end
