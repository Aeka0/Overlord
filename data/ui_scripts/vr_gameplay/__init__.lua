-- Original gameplay adapter; no extracted game scripts or decompiler dependency.
-- Native weaponInfo uses an independent idle watchdog, NOT hud_fade_ammodisplay.
-- Adapt only the ammo container instance. Keep all native ammo/text/pip updates,
-- and leave grenade, compass, prompts, names and global UI animation untouched.
if not Engine.GetDvarBool("vr_weaponHudNativeSource") then return end
local builder = LUI and LUI.MenuBuilder
local definitions = builder and builder.m_definitions
if not definitions or type(builder.registerDef) ~= "function" then
    print("[VR HUD] native definition registry unavailable; source adapter not installed")
    return
end

local function enabled()
    return Engine.GetDvarBool("vr_enable") and Engine.GetDvarBool("vr_weaponHudNativeSource")
end

-- The native pause constructor shares the finite animation pool with these
-- private HUD copies. Its first allocation precedes the next HUD refresh (and
-- that timer may stop entirely while paused), so yield leases at the boundary.
-- Weak keys must not keep a previous level's HUD or its Lua callbacks alive.
local sourceOwners = setmetatable({}, {__mode="k"})
local unpack = unpack or table.unpack
local pauseConstruction = 0
local function paused()
    if pauseConstruction > 0 then return true end
    if type(Engine.GetDvarInt) == "function" and (Engine.GetDvarInt("cl_paused") or 0) ~= 0 then return true end
    local root = LUI.roots and LUI.roots.UIRoot0
    local hud = root and root.hudManager and root.hudManager.hud
    return hud and hud.is_paused == true or false
end
local function pack(...) return {n=select("#",...),...} end
local function pauseFactory(original)
    return function(...)
        local released = false
        for owner in pairs(sourceOwners) do
            released = owner._vrReleaseAmmoSources() or released
        end
        if released then collectgarbage("collect") end
        pauseConstruction = pauseConstruction + 1
        local result = pack(pcall(original,...))
        pauseConstruction = pauseConstruction - 1
        if not result[1] then error(result[2],0) end
        return unpack(result,2,result.n)
    end
end
if type(builder.registerType) == "function" then
    local registerType = builder.registerType
    builder.registerType = function(name, factory, ...)
        if name == "sp_pause_menu" and type(factory) == "function" then factory = pauseFactory(factory) end
        return registerType(name,factory,...)
    end
end
if builder.m_types_build and type(builder.m_types_build.sp_pause_menu) == "function" then
    builder.m_types_build.sp_pause_menu = pauseFactory(builder.m_types_build.sp_pause_menu)
end

-- close() only detaches: an unfinished native transition can retain its Lua
-- callbacks and the entire discarded pip tree. Stop descendants before they
-- leave the update tree. Cancellation may dispatch transition_complete_*;
-- remove those callbacks first so a looping animation cannot rearm itself.
local function stopTree(element, retiring)
    local handlers = element.m_eventHandlers
    if type(handlers) == "table" then
        local names = {}
        for name in pairs(handlers) do
            if retiring or (type(name) == "string" and
                string.sub(name,1,19) == "transition_complete") then
                names[#names+1] = name
            end
        end
        for _,name in ipairs(names) do element:registerEventHandler(name,nil) end
    end
    local child = element:getFirstChild()
    while child do
        local nextChild = child:getNextSibling()
        stopTree(child,retiring)
        child = nextChild
    end
    element:cancelAnimateToState()
end

local function guardPipRemoval(root)
    local widget = root.primaryWidget
    if not widget or widget._vrPipRemovalGuard or type(widget.removeElement) ~= "function" then return end
    widget._vrPipRemovalGuard = true
    local remove = widget.removeElement
    widget.removeElement = function(self, child, ...)
        if child == root.pipImage then stopTree(child,false) end
        return remove(self,child,...)
    end
end

-- Construct each source with the installed game's factory. Native materials,
-- fonts, pip geometry and caliber labels remain owned by that factory.
-- The scope changes Lua READ queries only; no native selection/PS writes.
local adaptPipGraphic
local function container(alpha)
    return LUI.UIElement.new({topAnchor=true,bottomAnchor=true,leftAnchor=true,rightAnchor=true,
        top=0,bottom=0,left=0,right=0,alpha=alpha})
end
local function independentSources(parent, original)
    if not vr_weapon_hud or type(builder.buildItems) ~= "function" then return end
    local environment = package.loaded["LUI.sp_hud.weaponinfo"] or getfenv(original)
    local nativeGame = environment.Game
    if type(nativeGame) ~= "table" then return end
    -- Slots are cheap; native trees are leased only while the hand has a source.
    local sources = {}
    local sourceCount = vr_weapon_hud.count()
    for index=0,sourceCount-1 do sources[#sources+1] = {index=index} end
    -- Native capture enumerates sources in hand order. Lazy reconstruction
    -- must not change that order when the right hand is populated first.
    for _,source in ipairs(sources) do
        source.slot = container(1)
        parent:addElement(source.slot)
    end
    local function run(source, callback, ...)
        local priorGame = rawget(environment, "Game")
        local priorLabels = rawget(environment, "WeaponNameToLabel")
        source.data = source.data or {valid=false, loaded=0, reserve=0, capacity=0,
            maximumReserve=0, clipType=0, lowThreshold=0, name=""}
        source.proxy = source.proxy or setmetatable({
            GetPlayerWeaponName=function() return source.data.name end,
            GetPlayerWeaponDisplayName=function() return "" end,
            GetPlayerWeaponAttachmentsDisplayName=function() return "" end,
            GetPlayerMaxClipAmmo=function() return source.data.capacity end,
            GetPlayerClipAmmo=function() return source.data.loaded end,
            GetPlayerStockAmmo=function() return source.data.reserve end,
            GetPlayerMaxStockAmmo=function() return source.data.maximumReserve end,
            GetPlayerWeaponClipType=function() return source.data.clipType end,
            GetPlayerClipLowAmmoThreshold=function() return source.data.lowThreshold end,
            GetPlayerIsDualWielding=function() return false end,
            IsWeaponInAltMode=function() return false end,
            IsWeaponReloading=function() return 0 end,
            IsWeaponDropping=function() return 0 end,
            PlayerOnLadder=function() return false end
        }, {__index=nativeGame})
        rawset(environment, "Game", source.proxy)
        rawset(environment, "WeaponNameToLabel", source.labels)
        local success, result = pcall(callback, ...)
        rawset(environment, "Game", priorGame)
        rawset(environment, "WeaponNameToLabel", priorLabels)
        if not success then error(result) end
        return result
    end
    local function release(source)
        if not source.root then return false end
        stopTree(source.root,true)
        source.root:close()
        source.root, source.widget, source.graphic, source.labels = nil,nil,nil,nil
        source.visible = nil
        return true
    end
    parent._vrReleaseAmmoSources = function()
        local released = false
        for _,source in ipairs(sources) do
            released = release(source) or released
            source.failed = nil
            vr_weapon_hud.commit(source.index,false)
        end
        return released
    end
    sourceOwners[parent] = true
    local function build(source)
        local index = source.index
        source.labels = {}
        local definition = original()
        definition.id = "vrWeaponAmmoSource" .. index
        -- Remove only the private copy's timer before construction. Hiding a
        -- timer does not stop it; the original HUD owns the refresh cadence.
        for i=#(definition.children or {}),1,-1 do
            local child = definition.children[i]
            if child.type == "UITimer" and child.id == "weaponInfoRefreshTimer" then
                table.remove(definition.children,i)
            end
        end
        -- No second HUD refresh timer. The original refresh services these
        -- instance sources; keep native children attached for animation updates.
        local init = definition.handlers.init
        definition.handlers = {}
        local root = builder.buildItems(definition, {}, source.slot)
        source.root = root
        source.slot:addElement(root)
        run(source, init, root, {})
        local widget = root.primaryWidget
        -- Keep the established capture layout: ammo lives directly under the
        -- source, outside the native weaponInfoPanel's hidden parent chain.
        widget:close()
        local hidden = container(0)
        local child = root:getFirstChild()
        while child do
            -- Native onRefresh/watchdogs retain and animate compass, action
            -- slots and other non-ammo children. close() only detaches them;
            -- it neither destroys those references nor drains their animations.
            -- Native animateToState overwrites setAlpha on the same element.
            -- A MOD-owned ancestor keeps these live animation targets hidden.
            local nextChild = child:getNextSibling()
            child:close()
            hidden:addElement(child)
            child = nextChild
        end
        root:addElement(hidden)
        root:addElement(widget)
        root:registerAnimationState("vr_source", {topAnchor=true,bottomAnchor=true,
            leftAnchor=true,rightAnchor=true,top=-120*(sourceCount-1-index),
            bottom=-120*(sourceCount-1-index),left=0,right=0,alpha=1})
        root:animateToState("vr_source",0)
        source.root, source.widget = root, widget
        guardPipRemoval(root)
        local animate = widget.animateToState
        widget.animateToState = function(self, state, duration, ...)
            if state == "off" then self._state = "on"; state, duration = "on", 0 end
            return animate(self,state,duration,...)
        end
    end
    local nativeRefresh = parent.onRefresh
    local function setVisible(source, visible)
        if source.visible == visible then return end
        source.slot:setAlpha(visible and 1 or 0)
        source.visible = visible
    end
    local hiddenOriginal
    local function hideOriginal(widget)
        if hiddenOriginal and hiddenOriginal.widget == widget then return end
        if hiddenOriginal then
            local old = hiddenOriginal.widget
            if old:getParent() == hiddenOriginal.container then
                old:close()
                old:addElementBefore(hiddenOriginal.container)
            end
            hiddenOriginal.container:close()
            hiddenOriginal = nil
        end
        if widget then
            local hidden = container(0)
            hidden:addElementBefore(widget)
            widget:close()
            hidden:addElement(widget)
            hiddenOriginal = {widget=widget,container=hidden}
        end
    end
    parent.onRefresh = function(self, event)
        local active = enabled() and not paused() and not Engine.GetDvarBool("vr_hideHud") and vr_weapon_hud.active()
        local released = false
        -- Retire ALL stale owners before native refresh or any replacement
        -- allocation. In particular, pause must not retain a hidden 100-pip gun
        -- until after a menu/native refresh has already exhausted the pool.
        for _,source in ipairs(sources) do
            local data = vr_weapon_hud.source(source.index)
            local changed = source.name ~= data.name or source.capacity ~= data.capacity or
                source.clipType ~= data.clipType or source.definition ~= data.definition
            if not active or not data.valid or changed then
                released = release(source) or released
                source.failed = nil
            end
            source.name, source.capacity, source.clipType = data.name,data.capacity,data.clipType
            source.definition = data.definition
            source.prior, source.data = source.data,data
        end
        -- Only ownership transitions collect; stable refreshes allocate no trees
        -- and never run a full collection. Drop references before collecting.
        if released then collectgarbage("collect") end
        nativeRefresh(self,event)
        if not active then
            hideOriginal(nil)
            for _,source in ipairs(sources) do vr_weapon_hud.commit(source.index,false) end
            return
        end
        -- The original ammo source remains a native UI instance, but these
        -- independently populated native copies own its VR presentation.
        -- Keep native animation state/_state intact. Native refresh, watchdogs
        -- and transition_complete may animate this widget between our updates;
        -- the owned ancestor hides it without interrupting/restarting those fades.
        hideOriginal(self.primaryWidget)
        for _,source in ipairs(sources) do
            local prior = source.prior or {valid=false}
            if source.data.valid and not source.root and not source.failed then
                local built, buildError = pcall(build,source)
                if not built then
                    release(source)
                    collectgarbage("collect")
                    source.failed = true
                    print("[VR HUD] independent native source rejected: "..tostring(buildError))
                end
            end
            local root = source.root
            if root then
                setVisible(source,true)
                local data = source.data
                root.weaponWidgetDirty = root.weaponWidgetDirty or not prior.valid or
                    prior.name~=data.name or prior.loaded~=data.loaded or prior.reserve~=data.reserve or
                    prior.capacity~=data.capacity or prior.clipType~=data.clipType
                run(source,root.onRefresh,root,event)
                adaptPipGraphic(root)
                local graphic = root.pipImage
                if graphic and graphic.pipsRightHand then
                    -- Native watches observe the compatibility-selected gun.
                    -- Use the instance snapshot to animate the native pips.
                    if graphic.watcherRightHand then
                        graphic.watcherRightHand:registerEventHandler("int_watch_alert",nil)
                    end
                    local loaded = math.min(source.data.loaded,graphic.clipMax)
                    local low = source.data.loaded <= source.data.capacity*source.data.lowThreshold
                    for i=1,graphic.clipMax do
                        local pip = graphic.pipsRightHand[i]
                        local state = i<=loaded and (low and "low" or "on") or
                            (root.isNvgMode and "nvg_off" or "off")
                        if pip and (pip._vrState ~= state or source.graphic ~= graphic) then
                            pip:animateToState(state,0)
                            pip._vrState = state
                        end
                    end
                    source.graphic = graphic
                end
            end
            vr_weapon_hud.commit(source.index,root~=nil)
        end
    end
    parent._vrIndependentAmmoSources = sources
end

-- The native graphic allocates clipMax pips but indexes it with loaded ammo.
-- A closed-bolt +1 legitimately exceeds that graphic by ONE. Saturate that
-- single lookup to the last real pip; keep table length/layout, actual ammo,
-- numeric text and native event handling unchanged. Other missing indices
-- remain nil so unrelated errors are not silently hidden.
adaptPipGraphic = function(element)
    local graphic = element.pipImage
    local capacity = graphic and graphic.clipMax
    if type(capacity) ~= "number" or capacity < 1 or capacity > 1024 or
        capacity ~= math.floor(capacity) or element.dualWielding then return end
    local pips = graphic.pipsRightHand
    if type(pips) ~= "table" or getmetatable(pips) ~= nil or
        rawget(pips, capacity) == nil or rawget(pips, capacity + 1) ~= nil then return end
    setmetatable(pips, {__index = function(self, index)
        if index == capacity + 1 and enabled() and
            (Engine.GetDvarBool("vr_closedBoltChamber") or Engine.GetDvarBool("vr_physicalReload")) then
            return rawget(self, capacity)
        end
    end})
end

local function adapt(original)
    if type(original) ~= "function" then return original end
    return function(...)
        local definition = original(...)
        local init = definition.handlers and definition.handlers.init
        if type(init) ~= "function" then return definition end
        definition.handlers.init = function(element, event)
            init(element, event)
            -- Native refresh owns pip creation/replacement on weapon changes.
            -- Adapt each resulting instance, including cached graphics. Never
            -- replace global Game ammo queries or mutate native feed capacity.
            if not element._vrAmmoPipAdapter and type(element.onRefresh) == "function" then
                local refresh = element.onRefresh
                element._vrAmmoPipAdapter = true
                element.onRefresh = function(self, ...)
                    guardPipRemoval(self)
                    local result = refresh(self, ...)
                    adaptPipGraphic(self)
                    return result
                end
            end
            adaptPipGraphic(element)
            local widget = element.primaryWidget
            if not widget then return end
            -- The original selected-weapon HUD also replaces pip trees.
            guardPipRemoval(element)
            if widget._vrNativeAmmoAdapter then
                if not element._vrIndependentAmmoSources then independentSources(element,original) end
                return
            end
            local animate = widget.animateToState
            if type(animate) ~= "function" then return end
            widget._vrNativeAmmoAdapter = true
            widget.animateToState = function(self, state, duration, ...)
                if enabled() and state == "off" then
                    self._state = "on"
                    return animate(self, "on", 0)
                end
                return animate(self, state, duration, ...)
            end
            if enabled() then
                widget._state = "on"
                animate(widget, "on", 0)
            end
            print("[VR HUD] native ammo-container idle adapter installed")
            if not element._vrIndependentAmmoSources then independentSources(element,original) end
        end
        return definition
    end
end

-- Some HKS sessions register SP HUD definitions only when loading a level.
-- Preserve the registry's validation and every unrelated registration.
local register = builder.registerDef
builder.registerDef = function(name, factory, ...)
    if name == "WeaponInfoHudDef" then factory = adapt(factory) end
    return register(name, factory, ...)
end
if definitions.WeaponInfoHudDef then
    definitions.WeaponInfoHudDef = adapt(definitions.WeaponInfoHudDef)
end
