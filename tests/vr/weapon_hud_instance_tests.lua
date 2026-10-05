-- Synthetic widget tree tests; no extracted native layouts or assets.
local registrations=0
local transitions=0
local pending={}
local live=setmetatable({}, {__mode='k'})
local function node(initial)
    local n={children={},states={},alpha=initial and initial.alpha or 1,alphaWrites=0,m_eventHandlers={}}
    live[n]=true
    function n:addElement(child)
        child:close();self.children[#self.children+1]=child;child.parentNode=self
    end
    function n:close()
        if not self.parentNode then return end
        self.parentNode:removeElement(self)
    end
    function n:removeElement(child)
        for i,v in ipairs(self.children) do if v==child then table.remove(self.children,i);break end end
        child.parentNode=nil
    end
    function n:getParent()return self.parentNode end
    function n:addElementBefore(sibling)
        local parent=assert(sibling.parentNode)
        self:close()
        for i,v in ipairs(parent.children) do
            if v==sibling then table.insert(parent.children,i,self);self.parentNode=parent;return end
        end
        error('sibling not attached')
    end
    function n:getFirstChild()return self.children[1]end
    function n:getNextSibling()
        if self.parentNode then for i,v in ipairs(self.parentNode.children) do if v==self then return self.parentNode.children[i+1] end end end
    end
    function n:registerAnimationState(name,state)
        registrations=registrations+1
        self.states[name]=state
    end
    function n:setAlpha(alpha)self.alpha=alpha;self.alphaWrites=self.alphaWrites+1 end
    function n:animateToState(name)
        self.state=name
        local state=self.states[name]
        if state and state.alpha~=nil then self.alpha=state.alpha end
        transitions=transitions+1
        pending[self]=(pending[self] or 0)+1
        return name
    end
    function n:registerEventHandler(name,handler)self.m_eventHandlers[name]=handler end
    function n:cancelAnimateToState()
        pending[self]=nil
        for name,handler in pairs(self.m_eventHandlers) do
            if string.sub(name,1,19)=='transition_complete' then handler(self) end
        end
    end
    return n
end
local function effectiveAlpha(element)
    local alpha=1
    while element do alpha=alpha*element.alpha;element=element:getParent() end
    return alpha
end
-- A conservative lifecycle fixture: only attached nodes advance animations.
-- This is not a model of HKS's allocator or its exact capacity.
local function advance(root)
    pending[root]=nil
    for _,child in ipairs(root.children) do advance(child) end
end
local nativeGame={GetPlayerWeaponName=function()return 'native' end,
    GetPlayerMaxClipAmmo=function()return 9 end,GetPlayerClipAmmo=function()return 9 end,
    GetPlayerStockAmmo=function()return 90 end}
local originalLabels={original=true}
local environment={Game=nativeGame,WeaponNameToLabel=originalLabels}
package.loaded['LUI.sp_hud.weaponinfo']=environment
local hiddenHud=false
local pauseDvar=0
Engine={GetDvarBool=function(name)return name~='vr_hideHud' or hiddenHud end,
    GetDvarInt=function(name)return name=='cl_paused' and pauseDvar or 0 end}
local values={{valid=true,name='pistol',capacity=15,loaded=3,reserve=71,clipType=1,lowThreshold=.2,maximumReserve=150},
    {valid=true,name='rifle',capacity=30,loaded=20,reserve=88,clipType=4,lowThreshold=.2,maximumReserve=300},
    {valid=false,name='',capacity=0,clipType=0},{valid=false,name='',capacity=0,clipType=0}}
local active=true
local builds=0
local failBuild=false
local failNativeRefresh=false
local commits={0,0,0,0}
local published={}
vr_weapon_hud={count=function()return 4 end,source=function(index)return values[index+1]end,
    commit=function(index,visible)commits[index+1]=commits[index+1]+1;published[index+1]=visible end,
    active=function()return active end}
local function factory()
    return {children={{type='UITimer',id='weaponInfoRefreshTimer'},
        {type='UIElement',id='nativeContent'}},handlers={init=function(root)
        root.weaponPanel=node();root:addElement(root.weaponPanel)
        root.primaryWidget=node();root.weaponPanel:addElement(root.primaryWidget)
        root.compass=node();root:addElement(root.compass)
        root.compass.states.fade={alpha=1}
        root.compass.tickertape=node();root.compass:addElement(root.compass.tickertape)
        root.onRefresh=function(self)
            if environment.Game==nativeGame and failNativeRefresh then error('native refresh failed') end
            -- Native refresh retains references and updates these even when
            -- the adapter detached them. Include a descendant completion too.
            self.compass:animateToState('fade')
            self.compass.tickertape:animateToState('fade')
            local game=environment.Game
            if self.weaponWidgetDirty or self.weaponName==nil then
                self.weaponName=game.GetPlayerWeaponName()
                self.primaryStockAmmo=game.GetPlayerStockAmmo()
                self.primaryClipAmmo=game.GetPlayerClipAmmo()
                self.weaponWidgetDirty=false
            end
            local capacity=game.GetPlayerMaxClipAmmo()
            self.weaponClipSize=capacity
            if not self.pipImage or self.pipImage.clipMax~=capacity then
                if self.pipImage then self.primaryWidget:removeElement(self.pipImage) end
                self.pipImage=node()
                self.primaryWidget:addElement(self.pipImage)
                self.pipImage.clipMax=capacity
                self.pipImage.pipsRightHand={}
                self.pipImage.watcherRightHand=node()
                self.pipImage:addElement(self.pipImage.watcherRightHand)
                for i=1,capacity do
                    local pip=node()
                    self.pipImage:addElement(pip)
                    self.pipImage.pipsRightHand[i]=pip
                end
            end
        end
        environment.WeaponNameToLabel.synthetic=root.primaryWidget
        if environment.Game~=nativeGame and failBuild then error('native init failed') end
        root:onRefresh({})
    end}}
end
local privateTimers=0
local onPauseBuild
local builder={m_definitions={WeaponInfoHudDef=factory},
    m_types_build={sp_pause_menu=function(...)return onPauseBuild(...)end},
    registerDef=function()end,buildItems=function(definition)
    builds=builds+1
    local content=false
    for _,child in ipairs(definition.children) do
        if child.id=='weaponInfoRefreshTimer' then privateTimers=privateTimers+1 end
        if child.id=='nativeContent' then content=true end
    end
    assert(content, 'native source content must be preserved')
    return node()
end}
builder.registerType=function(name,ctor)
    assert(not builder.m_types_build[name], 'duplicate native type')
    builder.m_types_build[name]=ctor
end
LUI={MenuBuilder=builder,UIElement={new=node}}
dofile(adapter_path)
local parent=node()
builder.m_definitions.WeaponInfoHudDef().handlers.init(parent,{})
local sources=assert(parent._vrIndependentAmmoSources)
local function visibleOrder()
    local order={}
    local function walk(e)
        if effectiveAlpha(e)==0 then return end
        if e==parent.primaryWidget then order[#order+1]='original' end
        for _,source in ipairs(sources) do if e==source.widget then order[#order+1]=tostring(source.index) end end
        for _,child in ipairs(e.children) do walk(child) end
    end
    walk(parent)
    return table.concat(order,',')
end
assert(builds==0, 'native private trees must be lazy')
parent:onRefresh({})
assert(#sources==4 and sources[1].root.weaponName=='pistol' and sources[2].root.weaponName=='rifle')
assert(not sources[3].root and not sources[4].root and not published[3] and not published[4])
assert(sources[1].root.pipImage.clipMax==15 and sources[2].root.pipImage.clipMax==30)
assert(sources[1].root.primaryStockAmmo==71 and sources[2].root.primaryStockAmmo==88)
assert(sources[1].root.pipImage.pipsRightHand[3].state=='low')
assert(sources[1].root.pipImage.pipsRightHand[4].state=='off')
assert(sources[2].root.pipImage.pipsRightHand[20].state=='on')
assert(environment.Game==nativeGame and environment.WeaponNameToLabel==originalLabels)
for _,source in ipairs(sources) do
    if source.root then
    assert(source.root.compass:getParent():getParent()==source.root, 'native refresh targets must remain attached')
    assert(effectiveAlpha(source.root.compass)==0 and effectiveAlpha(source.widget)==1, 'only private non-ammo content is hidden')
    assert(source.root.compass.alpha==1, 'native fades can overwrite the element alpha')
    assert(source.widget.parentNode==source.root and effectiveAlpha(source.root.weaponPanel)==0,
        'ammo must leave the hidden native weapon panel while preserving capture coordinates')
    end
end
values[1]={valid=true,name='pistol',capacity=15,loaded=16,reserve=70,
    clipType=1,lowThreshold=.2,maximumReserve=150}
parent:onRefresh({})
assert(sources[1].root.primaryClipAmmo==16 and sources[1].root.primaryStockAmmo==70,
    'native ammo text must refresh when the instance snapshot changes')
assert(sources[1].root.pipImage.pipsRightHand[15].state=='on')
assert(sources[2].root.primaryStockAmmo==88 and sources[2].root.pipImage.pipsRightHand[20].state=='on')
values[1].valid=false;parent:onRefresh({})
assert(sources[1].root==nil and sources[1].graphic==nil and sources[1].labels==nil and sources[2].root.alpha==1)
values[1]={valid=true,name='replacement',capacity=7,loaded=2,reserve=14,clipType=1,lowThreshold=.2,maximumReserve=70}
parent:onRefresh({})
assert(sources[1].root.pipImage.clipMax==7 and sources[1].root.weaponName=='replacement')
assert(visibleOrder()=='0,1', 'rebuilding the left source after the right must preserve capture ownership order')
hiddenHud=true;parent:onRefresh({})
assert(not sources[1].root and not sources[2].root and not published[1] and not published[2], 'hiding HUD retires private ammo sources')
hiddenHud=false;parent:onRefresh({})
assert(sources[1].root and sources[2].root, 'showing HUD rebuilds the current native sources')
active=false;parent:onRefresh({})
assert(effectiveAlpha(parent.primaryWidget)==1 and sources[1].root==nil and sources[2].root==nil)
assert(environment.Game==nativeGame and environment.WeaponNameToLabel==originalLabels)
assert(commits[1]==commits[2] and commits[1]>=4)
for frame=1,1000 do
    active=frame%4~=0
    values[1].valid=frame%3~=0
    values[2].valid=frame%2~=0
    parent:onRefresh({})
    advance(parent)
    assert(next(pending)==nil, 'detached native animation targets accumulated pending transitions')
end
-- Native animations overwrite setAlpha on their target. Only an independent
-- ancestor can suppress the original source and the private non-ammo trees.
local oldWidget=parent.primaryWidget
parent.primaryWidget=node();active=true;values[1].valid=true;values[2].valid=true
parent:addElement(parent.primaryWidget)
parent.primaryWidget:addElement(parent.pipImage)
parent:onRefresh({});parent:onRefresh({})
local initialRegistrations=registrations
assert(effectiveAlpha(parent.primaryWidget)==0)
assert(effectiveAlpha(oldWidget)==1, 'replacement releases the previous widget ancestor')
parent.primaryWidget.states.native_fade={alpha=1}
parent.primaryWidget:animateToState('native_fade')
assert(parent.primaryWidget.alpha==1 and effectiveAlpha(parent.primaryWidget)==0 and parent.primaryWidget.state=='native_fade')
assert(visibleOrder()=='0,1', 'a native fade must not emit a third ammo border and stall capture')
local writes=parent.primaryWidget.alphaWrites
local originalAnimate=parent.primaryWidget.animateToState
parent.primaryWidget.animateToState=function()error('adapter must not restart the native visibility animation')end
for frame=1,10000 do parent:onRefresh({});advance(parent) end
assert(parent.primaryWidget.alphaWrites==writes, 'unchanged visibility must not be rewritten')
assert(registrations==initialRegistrations and next(pending)==nil)
parent.primaryWidget.animateToState=originalAnimate
active=false;parent:onRefresh({})
assert(effectiveAlpha(parent.primaryWidget)==1 and parent.primaryWidget.state=='native_fade')
active=true;parent:onRefresh({})
assert(effectiveAlpha(parent.primaryWidget)==0)
assert(privateTimers==0, 'private source must remove its refresh timer before construction')
-- Pool pressure depends on retained trees, not lifetime registration calls.
-- Model native transitions as strong roots and check retired trees through weak
-- references. Cancelling must clear looping completion callbacks first.
local retired=setmetatable({}, {__mode='v'})
local function retainLoop(element)
    element:animateToState('low')
    element:registerEventHandler('transition_complete_low',function(self)self:animateToState('low')end)
end
for _,source in ipairs(sources) do
    if source.root then
        retired[#retired+1]=source.root
        retainLoop(source.root.compass.tickertape)
        retainLoop(source.root.pipImage.pipsRightHand[1])
    end
end
active=false;failNativeRefresh=true
assert(not pcall(parent.onRefresh,parent,{}))
assert(sources[1].root==nil and sources[2].root==nil, 'cleanup must precede failing native refresh')
collectgarbage('collect')
assert(next(retired)==nil, 'retired sources remain strongly referenced')
failNativeRefresh=false
-- Failed construction is cleaned up and is not retried every 10ms.
active=true;failBuild=true
parent:onRefresh({})
local failedBuilds=builds
for i=1,100 do parent:onRefresh({});advance(parent) end
assert(builds==failedBuilds and sources[1].root==nil and sources[2].root==nil)
assert(environment.Game==nativeGame and environment.WeaponNameToLabel==originalLabels)
active=false;parent:onRefresh({});failBuild=false;active=true;parent:onRefresh({})
assert(sources[1].root and sources[2].root)
-- Both hands may carry a primary feed and a separate underbarrel feed. Native
-- construction order and independent shots/reloads cannot exchange the rows.
values[4]={valid=true,name='fal_shotgun_attach',definition=60,capacity=4,loaded=4,reserve=23,
    clipType=9,lowThreshold=.33,maximumReserve=40}
parent:onRefresh({})
assert(visibleOrder()=='0,1,3')
values[3]={valid=true,name='m203_m4',definition=58,capacity=1,loaded=1,reserve=8,
    clipType=10,lowThreshold=.33,maximumReserve=10}
parent:onRefresh({})
assert(visibleOrder()=='0,1,2,3', 'secondary rows keep stable order even when the right module is created first')
local primaryClip=sources[1].root.primaryClipAmmo
local primaryReserve=sources[1].root.primaryStockAmmo
values[3]={valid=true,name='m203_m4',definition=58,capacity=1,loaded=0,reserve=8,
    clipType=10,lowThreshold=.33,maximumReserve=10}
parent:onRefresh({})
assert(sources[3].root.primaryClipAmmo==0 and sources[3].root.pipImage.pipsRightHand[1].state=='off')
assert(sources[4].root.primaryClipAmmo==4 and sources[4].root.primaryStockAmmo==23)
values[3]={valid=true,name='m203_m4',definition=58,capacity=1,loaded=1,reserve=7,
    clipType=10,lowThreshold=.33,maximumReserve=10}
values[4]={valid=true,name='fal_shotgun_attach',definition=60,capacity=4,loaded=3,reserve=23,
    clipType=9,lowThreshold=.33,maximumReserve=40}
parent:onRefresh({})
assert(sources[3].root.primaryClipAmmo==1 and sources[3].root.primaryStockAmmo==7)
assert(sources[4].root.primaryClipAmmo==3 and sources[4].root.pipImage.pipsRightHand[4].state=='off')
assert(sources[1].root.primaryClipAmmo==primaryClip and sources[1].root.primaryStockAmmo==primaryReserve)
for i=1,4 do
    assert(published[i])
    assert(sources[i].root.states.vr_source.top==-120*(4-i), 'all four native rows have separate capture lanes')
end
values[3]={valid=false,name='',capacity=0,clipType=0}
values[4]={valid=false,name='',capacity=0,clipType=0}
parent:onRefresh({});advance(parent)
assert(visibleOrder()=='0,1' and not sources[3].root and not sources[4].root)
assert(not published[3] and not published[4], 'removed modules cannot retain a capture owner')
values[3]={valid=true,name='m203_m4',definition=58,capacity=1,loaded=1,reserve=7,
    clipType=10,lowThreshold=.33,maximumReserve=10}
failBuild=true;parent:onRefresh({});failBuild=false
assert(not sources[3].root and not published[3], 'a failed module UI must not stall other captures by publishing an absent border')
values[3]={valid=false,name='',capacity=0,clipType=0};parent:onRefresh({});advance(parent)
-- Large-clip replacement and empty hands release old pips before allocating
-- replacements, across repeated hand transfers and menu cycles.
for i=1,150 do
    for h=1,2 do
        retired[h]=sources[h].root
        if sources[h].root then retainLoop(sources[h].root.pipImage.pipsRightHand[1]) end
        values[h]={valid=true,name='gun'..i,capacity=i%2==0 and 100 or 30,
            loaded=1,reserve=200,clipType=1,lowThreshold=.2,maximumReserve=300}
    end
    parent:onRefresh({});advance(parent);collectgarbage('collect')
    assert(next(retired)==nil, 'replacement retained a retired large-clip tree')
    local count=0
    for _ in pairs(live) do count=count+1 end
    assert(count<300, 'live UI elements grow with weapon changes')
end
-- The selected-weapon native HUD also detaches old graphics during refresh.
retired[1]=parent.pipImage
retainLoop(parent.pipImage.pipsRightHand[1])
nativeGame.GetPlayerMaxClipAmmo=function()return 100 end
parent:onRefresh({});advance(parent);collectgarbage('collect')
assert(retired[1]==nil, 'original HUD replacement retained its old animation tree')
local menu=node()
for i=1,32 do menu:registerAnimationState('menu'..i,{alpha=1}) end
-- Pause allocation happens before the next HUD tick. Native carry remains
-- active and its feed snapshot remains valid throughout the menu: neither can
-- retire the private copies on its own. Low-ammo callbacks also retain roots.
values[3]={valid=true,name='launcher',capacity=1,loaded=1,reserve=7,clipType=10,lowThreshold=.33}
values[4]={valid=true,name='shotgun',capacity=4,loaded=3,reserve=23,clipType=9,lowThreshold=.33}
parent:onRefresh({});advance(parent)
local function retainSources()
    for i,source in ipairs(sources) do
        assert(source.root)
        retired[i]=source.root
        retainLoop(source.root.pipImage.pipsRightHand[1])
    end
end
local function assertRetired()
    for i,source in ipairs(sources) do
        assert(not source.root and not source.graphic and not source.widget and not source.labels and not published[i],
            'pause must retire every feed before the first native menu allocation')
    end
    assert(next(retired)==nil, 'private HUD animations still own pool capacity when pause constructs')
end
local marker={}
onPauseBuild=function(arg,trailing)
    assert(arg==marker and trailing==nil)
    assertRetired()
    -- A synchronous refresh during construction cannot reacquire that budget,
    -- even before the engine publishes cl_paused or the menu stack entry.
    local before=builds
    parent:onRefresh({})
    assert(builds==before, 'reentrant refresh rebuilt HUD during pause construction')
    return marker,nil,'native-menu'
end
retainSources()
local a,b,c=builder.m_types_build.sp_pause_menu(marker,nil)
assert(a==marker and b==nil and c=='native-menu', 'pause preserves native arguments and return values')
pauseDvar=1
local pauseBuilds=builds
for i=1,20 do parent:onRefresh({});advance(parent) end
assert(builds==pauseBuilds and active, 'paused native carry cannot rebuild private sources')
pauseDvar=0
LUI.roots={UIRoot0={hudManager={hud={is_paused=true}}}}
parent:onRefresh({})
assert(builds==pauseBuilds, 'native HUD pause state covers dvar publication order')
LUI.roots.UIRoot0.hudManager.hud.is_paused=false
parent:onRefresh({});advance(parent)
assert(visibleOrder()=='0,1,2,3', 'resume rebuilds current feeds in stable capture order')

-- Late native registration, constructor failure, and repeated pause cycles use
-- the same pre-allocation boundary. A failed constructor must not latch a
-- synthetic pause or swallow the native error.
builder.m_types_build.sp_pause_menu=nil
builder.registerType('sp_pause_menu',function()
    assertRetired()
    error('native pause construction failed')
end)
retainSources()
local ok,err=pcall(builder.m_types_build.sp_pause_menu)
assert(not ok and string.find(err,'native pause construction failed',1,true))
parent:onRefresh({});advance(parent)
assert(visibleOrder()=='0,1,2,3', 'failed pause constructor must release its construction guard')
builder.m_types_build.sp_pause_menu=nil
builder.registerType('sp_pause_menu',onPauseBuild)
for i=1,8 do
    retainSources()
    builder.m_types_build.sp_pause_menu(marker)
    pauseDvar=1;parent:onRefresh({});advance(parent)
    pauseDvar=0;parent:onRefresh({});advance(parent)
end
local unrelated=function()return 'unchanged' end
builder.registerType('unrelated_menu',unrelated)
assert(builder.m_types_build.unrelated_menu==unrelated, 'unrelated constructors retain their original behavior')
print('weapon HUD instance tests: PASS (independent feeds, reclamation, pause allocation boundary, resume, failure ordering, bounded construction)')
