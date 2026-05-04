function BeginPlay(owner)
    if owner == nil then
        print("[CleaningTool][Lua] Sponge BeginPlay owner=nil")
        return
    end

    local registered = RegisterCleaningToolActor(owner, "sponge")
    print("[CleaningTool][Lua] Register sponge actor=" .. owner:GetName() .. " result=" .. tostring(registered))
end
