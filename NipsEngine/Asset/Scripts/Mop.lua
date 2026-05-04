function BeginPlay(owner)
    if owner == nil then
        print("[CleaningTool][Lua] Mop BeginPlay owner=nil")
        return
    end

    local registered = RegisterCleaningToolActor(owner, "mop")
    print("[CleaningTool][Lua] Register mop actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end
