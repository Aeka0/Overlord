-- Small fake native widget/registry, not extracted game implementation.
local flags = {vr_enable = true, vr_weaponHudNativeSource = true, vr_closedBoltChamber = true}
Engine = {GetDvarBool = function(name) return flags[name] == true end}
local function widget()
    return {animateToState = function(self, name, duration, extra)
        self.state, self.duration, self.extra = name, duration, extra
        self.draws = (self.draws or 0) + 1
        return "native-result"
    end}
end
local function factory()
    return {handlers = {init = function(element, event)
        element.initEvent = event
        element.primaryWidget, element.grenade = widget(), widget()
    end}}
end
local function reset(early)
    local defs = {}
    if early then defs.WeaponInfoHudDef = factory end
    LUI = {MenuBuilder = {m_definitions = defs, registerDef = function(name, def)
        assert(not defs[name], "duplicate")
        defs[name] = def
    end}}
end
for _, early in ipairs({true, false}) do
    reset(early)
    dofile(adapter_path)
    if not early then LUI.MenuBuilder.registerDef("WeaponInfoHudDef", factory) end
    local element, event = {}, {}
    LUI.MenuBuilder.m_definitions.WeaponInfoHudDef().handlers.init(element, event)
    assert(element.initEvent == event and element.primaryWidget.state == "on")
    -- Native watchdog can fire repeatedly, even after minutes of inactivity.
    for seconds = 10, 120, 10 do
        element.primaryWidget._state = "off"
        element.primaryWidget:animateToState("off", 2000)
        element.grenade:animateToState("off", 2000)
        assert(element.primaryWidget.state == "on" and element.primaryWidget.duration == 0)
        assert(element.primaryWidget._state == "on")
        assert(element.grenade.state == "off" and element.grenade.duration == 2000)
    end
    assert(element.primaryWidget:animateToState("nvg_on", 50, "pass-through") == "native-result")
    assert(element.primaryWidget.state == "nvg_on" and element.primaryWidget.extra == "pass-through")
    flags.vr_enable = false
    element.primaryWidget:animateToState("off", 2000)
    assert(element.primaryWidget.state == "off" and element.primaryWidget.duration == 2000)
    flags.vr_enable = true
    local unrelated = function() end
    LUI.MenuBuilder.registerDef("Unrelated", unrelated)
    assert(LUI.MenuBuilder.m_definitions.Unrelated == unrelated)
    assert(not pcall(LUI.MenuBuilder.registerDef, "Unrelated", unrelated))
end
-- Synthetic finite native pip array and its one-based update contract. No
-- extracted implementation; preserve real loaded ammo and every real pip.
local function graphic(capacity)
    local result = {clipMax = capacity, pipsRightHand = {}}
    for n = 1, capacity do result.pipsRightHand[n] = widget() end
    return result
end
local function paint(graphic, ammo)
    for n = 1, ammo do graphic.pipsRightHand[n]:animateToState("on", 0) end
    for n = ammo + 1, graphic.clipMax do graphic.pipsRightHand[n]:animateToState("off", 0) end
end
for _, early in ipairs({true, false}) do
    reset(false)
    dofile(adapter_path)
    LUI.MenuBuilder.registerDef("WeaponInfoHudDef", function()
        return {handlers = {init = function(element)
            element.primaryWidget = widget()
            if early then element.pipImage = graphic(15) end
            element.onRefresh = function(self, replacement, marker)
                if replacement then self.pipImage = replacement end
                return marker
            end
        end}}
    end)
    local element = {}
    LUI.MenuBuilder.m_definitions.WeaponInfoHudDef().handlers.init(element, {})
    if not early then assert(element:onRefresh(graphic(15), "refreshed") == "refreshed") end
    for _, capacity in ipairs({15, 7, 12, 30}) do
        assert(element:onRefresh(graphic(capacity), "switched") == "switched")
        local g = element.pipImage
        assert(#g.pipsRightHand == capacity and rawget(g.pipsRightHand, capacity + 1) == nil)
        assert(g.pipsRightHand[capacity + 1] == g.pipsRightHand[capacity])
        paint(g, capacity + 1); paint(g, capacity); paint(g, capacity - 1)
        assert(g.pipsRightHand[capacity].state == "off")
        paint(g, 0)
        for n = 1, capacity do assert(g.pipsRightHand[n].state == "off") end
        assert(not pcall(paint, g, capacity + 2), "unexpected overflow must not be swallowed")
        local mt = getmetatable(g.pipsRightHand)
        element:onRefresh(g)
        assert(getmetatable(g.pipsRightHand) == mt, "cached graphics are adapted once")
        flags.vr_enable = false
        assert(g.pipsRightHand[capacity + 1] == nil, "desktop stays native")
        flags.vr_enable = true; flags.vr_closedBoltChamber = false
        assert(g.pipsRightHand[capacity + 1] == nil)
        flags.vr_physicalReload = true
        assert(g.pipsRightHand[capacity + 1] == g.pipsRightHand[capacity])
        flags.vr_physicalReload = false; flags.vr_closedBoltChamber = true
    end
    local foreign = graphic(15)
    local custom = {__index = function() return "owned by native/another adapter" end}
    setmetatable(foreign.pipsRightHand, custom)
    element:onRefresh(foreign)
    assert(getmetatable(foreign.pipsRightHand) == custom)
    element.dualWielding = true
    local dual = graphic(15); element:onRefresh(dual)
    assert(getmetatable(dual.pipsRightHand) == nil, "unreviewed dual wield remains unchanged")
    element.dualWielding = false
    for _, invalid in ipairs({-1, 0, 1.5, 1025, math.huge, "15"}) do
        local bad = {clipMax = invalid, pipsRightHand = {}}
        element:onRefresh(bad)
        assert(getmetatable(bad.pipsRightHand) == nil)
    end
    element:onRefresh({})
end
-- Release/no-capability session must not alter the registry at all.
reset(true)
flags.vr_weaponHudNativeSource = false
local register = LUI.MenuBuilder.registerDef
dofile(adapter_path)
assert(LUI.MenuBuilder.registerDef == register and LUI.MenuBuilder.m_definitions.WeaponInfoHudDef == factory)
print("weapon HUD LUI tests: PASS (idle, isolated widget, native pass-through, late registration, bounded plus-one pips, capability)")
