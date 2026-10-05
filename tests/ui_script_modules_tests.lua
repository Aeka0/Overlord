-- Exercise the real common/button scripts against a small widget model. This
-- reproduces duplicate registration; it is not extracted native LUI code.
unpack = unpack or table.unpack
local native_failure = false
local function reset()
    local native_scroll = function(list, _, options)
        if native_failure then error('native menu fixture failure') end
        if list.count > (options and options.rows or 10) then
            if list.scrollbar then error('Element has duplicate id "LUIVerticalScrollbar"') end
            list.scrollbar = true
        end
    end
    LUI = {MenuBuilder = {m_types_build = {}}, Options = {
        InitScrollingList = native_scroll,
        AddOptionTextInfo = function() return {} end
    }}
    Engine = {Localize = function(text) return text end}
    LUI.MenuBuilder.m_types_build.main_campaign = function()
        local list = {count = 9, removeElement = function() end, insertElement = function() end}
        local menu = {type = 'main_campaign', list = list, added = {}}
        function menu:AddButton(text)
            self.list.count = self.list.count + 1
            table.insert(self.added, text)
            return {}
        end
        function menu:getChildById() return self.list end
        function menu:removeElement() end
        LUI.Options.InitScrollingList(list)
        LUI.Options.AddOptionTextInfo(menu)
        return menu
    end
    dofile('src/client/resources/ui_scripts/common.lua')
    return native_scroll
end

local original = reset()
for _, script in ipairs(duplicate_initializers) do dofile(script) end
local ok, failure = pcall(LUI.MenuBuilder.m_types_build.main_campaign)
assert(not ok and tostring(failure):find('duplicate id "LUIVerticalScrollbar"', 1, true),
    'executing both physical copies reproduces the reported failure')
assert(LUI.Options.InitScrollingList == original, 'temporary wrapper restored even on duplicate registration failure')

original = reset()
assert(#selected_initializers == 1)
for _, script in ipairs(selected_initializers) do dofile(script) end
for attempt = 1, 3 do
    local menu = LUI.MenuBuilder.m_types_build.main_campaign()
    assert(#menu.added == 2 and menu.list.count == 11 and menu.list.scrollbar)
    assert(menu.added[1] == '@MENU_MODS' and menu.added[2] == '@LUA_MENU_ACHIEVEMENTS')
    assert(LUI.Options.InitScrollingList == original, 'normal menu reopening cannot accumulate wrappers')
end
native_failure = true
ok, failure = pcall(LUI.MenuBuilder.m_types_build.main_campaign)
assert(not ok and tostring(failure):find('native menu fixture failure', 1, true))
assert(LUI.Options.InitScrollingList == original, 'a native error cannot leak the temporary list override')
native_failure = false
assert(#LUI.MenuBuilder.m_types_build.main_campaign().added == 2, 'menu recovers after an unrelated construction error')

-- Paused content must not register popups/widgets or load dependent modules.
local original_require = require
require = function() error('paused MOTD attempted to load a UI module') end
for _, state in ipairs({{}, {isenabled = function() return false end}}) do
    motd = state
    dofile('data/cdata/ui_scripts/motd/__init__.lua')
    dofile('data/cdata/ui_scripts/wordle/__init__.lua')
end
local loaded = {}
motd = {isenabled = function() return true end}
require = function(name) table.insert(loaded, name) end
dofile('data/cdata/ui_scripts/motd/__init__.lua')
assert(#loaded == 2 and loaded[1] == 'motd' and loaded[2] == 'featured', 'retained modules can be restored through the shared capability')
require = original_require

Engine.InFrontend = function() return true end
updater = {updatesavailable = function() return false end}
LUI = {}
dofile('src/client/resources/ui_scripts/updater.lua')
assert(type(LUI.tryupdating) == 'function', 'paused updater keeps a harmless compatibility entry')
LUI.tryupdating(false) -- No backend access, timers, popup factories or event registration.
