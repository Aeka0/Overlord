local callback
LUI = {onmenuopen = function(name, action)
    assert(name == "main_campaign")
    callback = action
end}
Engine = {InFrontend = function() return false end}
dofile('src/client/resources/ui_scripts/campaign_unlock.lua')
assert(callback == nil, 'gameplay VM must not register the profile action')

Engine.InFrontend = function() return true end
dofile('src/client/resources/ui_scripts/campaign_unlock.lua')
assert(type(callback) == 'function')
for _, initially_unlocked in ipairs({true, false}) do
    local profile = {unlocked = initially_unlocked, intel = 7, difficulty = '123'}
    local dvar
    local commands = {}
    Engine.ExecNow = function(command)
        commands[#commands + 1] = command
        if command == 'profile_menuDvarsSetup' then dvar = profile.unlocked end
        if command == 'profile_menuDvarsFinish' then profile.unlocked = dvar end
    end
    Engine.GetDvarBool = function(name)
        assert(name == 'profileMenuOption_hasUnlockedAll_SP')
        return dvar
    end
    Engine.SetDvarBool = function(name, value)
        assert(name == 'profileMenuOption_hasUnlockedAll_SP' and #commands == 1)
        dvar = value
    end
    game = {applycampaignunlock = function(_, apply) apply() end}
    callback()
    assert(table.concat(commands, ',') == 'profile_menuDvarsSetup,profile_menuDvarsFinish,updategamerprofile,profile_menuDvarsSetup')
    assert(profile.unlocked == true and profile.intel == 7 and profile.difficulty == '123')
    Engine.GetDvarBool = function() return nil end
    assert(not pcall(callback), 'missing profile cannot silently report success')
    commands = {}
    local reads = 0
    Engine.GetDvarBool = function()
        reads = reads + 1
        return false
    end
    assert(not pcall(callback) and reads == 2, 'profile readback must validate the requested value')
end
