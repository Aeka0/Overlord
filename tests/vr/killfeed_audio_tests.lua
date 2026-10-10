-- Run the shipped script with damage/death ordering and disposable entities.
local style, callback, sounds, timers, duck_calls
local function entity(number)
    local value = {alive = true, number = number}
    function value:getentref() return self.number, 0 end
    function value:onnotifyonce(name, fn)
        assert(name == "death")
        local listener = {clear = function() self.death = nil end}
        self.death = fn
        return listener
    end
    function value:playlocalsound(alias) table.insert(sounds, alias) end
    return value
end
local function reset(selected)
    style, sounds, timers, duck_calls = selected, {}, {}, {}
    player = entity(0)
    game = {}
    function game:getdvar(name) assert(name == "vr_killfeedStyle"); return style end
    function game:isalive(victim)
        if victim.removed then error("entity removed") end
        return victim.alive and 1 or 0
    end
    function game:scriptcall(script, action)
        assert(script == "scripts/killfeed_ducking")
        assert(action == "trigger", "GSC owns activation and recovery together")
        table.insert(duck_calls, action)
        return 49
    end
    function game:ontimeout(fn, delay)
        local timer = {fn = fn, delay = delay}
        function timer:clear() self.cleared = true end
        table.insert(timers, timer)
        return timer
    end
    function game:onentitydamage(fn) callback = fn end
    dofile("data/cdata/scripts/killfeed/__init__.lua")
end
local function flush(delay)
    local ready = {}
    for _, timer in ipairs(timers) do
        if not timer.cleared and timer.delay == delay then
            timer.cleared = true
            table.insert(ready, timer)
        end
    end
    for _, timer in ipairs(ready) do timer.fn() end
end
local function hit(victim, attacker, damage, location)
    assert(callback(victim, nil, attacker or player, damage or 10, "MOD_RIFLE_BULLET", nil, nil, location or "torso") == nil,
        "feedback cannot replace native damage")
end
reset("mw2019")
local victim = entity(1)
hit(victim); assert(#sounds == 0); flush(0)
assert(#sounds == 1 and sounds[1] == "kf_mw2019_hit")
hit(victim, nil, 10, "helmet"); victim.alive = false; victim.death(); flush(0)
assert(#sounds == 2 and sounds[2] == "kf_mw2019_kill_headshot", "death precedes fallback without duplicate hit")
victim = entity(2); hit(victim); victim.alive = false; flush(0)
assert(sounds[3] == "kf_mw2019_kill", "next-frame lethal fallback")
assert(#duck_calls == 3, "each feedback refreshes the native-script deadline")
assert(#timers == 3, "only next-frame damage resolution uses Lua timers; ducking has no Lua timer")
hit(victim); hit(player); hit(entity(3), entity(4)); hit(entity(5), nil, 0); flush(0)
assert(#sounds == 3, "dead targets, self, NPC attackers and zero damage are ignored")
victim = entity(6); hit(victim); victim.removed = true; flush(0)
assert(#sounds == 3 and victim.death == nil, "removed target retires pending work without inventing a kill")
for _, disabled in ipairs({"off", "unknown", "mw2019;quit"}) do
    reset(disabled); hit(entity(1)); flush(0)
    assert(#sounds == 0 and #duck_calls == 0)
end
reset("bocw")
for index = 1, 4 do hit(entity(index)); flush(0) end
assert(table.concat(sounds, ",") == "kf_bocw_hit_1,kf_bocw_hit_2,kf_bocw_hit_3,kf_bocw_hit_1")
victim = entity(5); hit(victim, nil, 10, "head"); victim.death(); flush(0)
assert(sounds[5] == "kf_bocw_headshot_1" and sounds[6] == "kf_bocw_kill", "BOCW layers its kill sound")
reset("mw_classic"); victim = entity(1)
hit(victim); flush(0); hit(victim); victim.death(); flush(0)
assert(#sounds == 2 and sounds[1] == "kf_mw_classic_hit" and sounds[2] == sounds[1])
reset("mw2019")
game.scriptcall = function() error("native script unavailable") end
hit(entity(1)); flush(0)
assert(sounds[1] == "kf_mw2019_hit", "ducking failure cannot suppress feedback audio")
game.scriptcall = function() error("failed ducking must not be submitted again") end
hit(entity(2)); flush(0)
assert(#sounds == 2 and sounds[2] == "kf_mw2019_hit")
print("killfeed audio tests passed")
