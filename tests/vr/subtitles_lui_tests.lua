local callback, selected, current, pending, writes, saves, controller, failures
local function reset(frontend, requested, existing)
    callback, selected, current, pending = nil, requested, existing, true
    writes, saves, controller, failures = 0, 0, 2, 0
    Engine = {
        InFrontend = function() return frontend end,
        GetFirstActiveController = function() return controller end,
        SubtitlesEnabled = function(index) assert(index == controller); return current end,
        SetSubtitlesEnabled = function(index, enabled)
            assert(index == controller and type(enabled) == "boolean")
            writes, current = writes + 1, enabled
        end,
        ExecNow = function(command, index)
            assert(command == "updategamerprofile" and index == controller)
            saves = saves + 1
        end,
    }
    LUI = {onmenuopen = function(menu, fn) assert(menu == "main_campaign"); callback = fn end}
    game = {applysubtitles = function(_, fn)
        if not pending then return end
        pending = false -- Mirrors the process-owned native dispatch fence.
        if not pcall(fn, selected) then failures = failures + 1 end
    end}
    dofile(adapter_path)
    assert(writes == 0 and saves == 0, "VM initialization cannot touch an unloaded player profile")
end
for _, requested in ipairs({false, true}) do
    for _, existing in ipairs({false, true}) do
        reset(true, requested, existing)
        assert(callback, "Frontend registers the profile-ready callback")
        callback()
        local changed = requested ~= existing and 1 or 0
        assert(current == requested and writes == changed and saves == changed)
        current = not requested -- A later choice in the native Audio menu.
        callback()
        dofile(adapter_path) -- HKS may restart while the process remains alive.
        callback()
        assert(current ~= requested and writes == changed, "Menu returns and VM reloads retain later native choices")
    end
end
reset(false, false, true)
assert(callback == nil, "Gameplay VM cannot apply startup profile preferences")
reset(true, false, true)
controller = -1
callback()
assert(writes == 0 and saves == 0 and failures == 1, "An unavailable controller cannot modify a profile")
print("Subtitle profile adapter tests passed")
