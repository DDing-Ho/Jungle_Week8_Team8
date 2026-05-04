-- LuaScriptComponent contract
-- owner: AActor bound from C++
-- otherActor: AActor or nil
-- hit: FHitResult
-- Log(message): writes to the editor console
-- StartCoroutine(function() ... end), wait(seconds): coroutine helpers

local CLEAN_RADIUS   = 0.09   -- 마스크 UV 반지름 (0~1 기준)
local CLEAN_STRENGTH = 60     -- 지울 강도 (uint8, 클수록 빠르게 지워짐)
local REACH_DISTANCE = 300.0  -- 레이캐스트 최대 거리

function BeginPlay(owner)
    print("Player Script Started: ", owner:GetName())
end

function EndPlay(owner)
end

function OnOverlap(owner, otherActor)
end

function OnEndOverlap(owner, otherActor)
end

function OnHit(owner, hit)
end

function OnInteract(owner, interactor)
end

function Tick(owner, deltaTime)
    local hit = RaycastCenter(REACH_DISTANCE)
    if not hit or not hit.bHit then
        print("NoHit")
        return
    end

    -- 데칼을 맞췄는지 확인
    local decal = hit:GetDecalComponent()
    if not decal then
        return
    end

    -- 마우스 왼쪽 클릭 / 홀딩 중일 때 청소
    if GetKey(KEY_LEFT_MOUSE) then
        decal:PaintAtWorldPos(hit.Location, CLEAN_RADIUS, CLEAN_STRENGTH)

        local pct = decal:GetCleanPercentage()
        --SetProgress(pct)
    end
end
