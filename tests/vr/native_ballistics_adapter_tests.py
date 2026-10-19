"""Exercise the production fire_owned transaction with deterministic engine endpoints."""
from pathlib import Path
import json
import os
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / 'src/client/component/vr/gameplay/native_ballistics.cpp').read_text(encoding='utf-8')
# Keep the actual overloads and complete transaction, replacing only engine endpoints.
body = source[source.index('\tresult fire_owned('):]
out = root / 'build/native-ballistics-adapter-tests'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include "component/vr/gameplay/native_ballistics.hpp"
#include "component/vr/gameplay/muzzle_clearance.hpp"
#include <gsl/gsl>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>
using namespace vr::gameplay::weapons;
namespace vr::gameplay::weapons::hei_round {
struct shot_scope { explicit shot_scope(weapon_identity) {} void complete() {} };
}
namespace fixture {
int grenade_mode=0;
bool grenade_prepare=true;
bool enabled=true, allowed=true, server=true, blocked=false, commit_ok=true, settle_ok=true, sustain=false;
std::uint64_t epoch=1;
std::map<std::uint64_t,native_ammunition::snapshot> clips;
std::array<weapon_identity,512> projected{};
std::array<void*,512> definitions{};
std::vector<std::string> events;
std::vector<std::string> names;
std::vector<unsigned> bullets;
std::function<void()> on_notify, on_settle;
bool invalid_name=false;
}
namespace game {
union Weapon { struct { unsigned index:8, rest:24; } fields; unsigned data; };
struct playerState_s { int commandTime=1000, e_flags=0; float origin[3]{}, viewHeightCurrent=0; } player;
using gclient_s=playerState_s;
struct gentity_s { struct { unsigned short entityNum=0; } s; gclient_s* client=&player; } g_entities[1];
struct scr_entref_t { unsigned short entnum, classnum; };
struct dvar_t { struct { int integer=0; } current; } paused;
int catcher=0; int* keyCatchers=&catcher;
bool CL_IsCgameInitialized() { return fixture::enabled; }
const dvar_t* Dvar_FindVar(const char* name) { assert(std::string(name)=="cl_paused"); return &paused; }
struct trace_t { float fraction=1; bool allsolid=false,startsolid=false; };
struct Bounds {};
void G_TraceCapsule(trace_t* trace,const float*,const float*,const Bounds*,int,int) {
    trace->fraction=fixture::blocked ? 0.f : 1.f;
}
void BG_GetWeaponNameComplete(Weapon weapon,bool alternate,char* name,unsigned size) {
    assert(!alternate && size==1024);
    if(fixture::invalid_name) { std::memset(name,'x',size); return; }
    const auto value=std::string("weapon_")+std::to_string(weapon.data)+"_silencer";
    std::memcpy(name,value.c_str(),value.size()+1);
}
}
namespace scripting {
struct entity { game::scr_entref_t ref; };
struct script_value { std::string text; script_value(std::string s):text(std::move(s)){} };
void notify(const entity& target,const std::string& event,const std::vector<script_value>& args) {
    assert(target.ref.entnum==0 && target.ref.classnum==0 && event=="weapon_fired" && args.size()==1);
    fixture::events.push_back("notify"); fixture::names.push_back(args[0].text);
    if(fixture::on_notify) fixture::on_notify();
}
}
namespace scheduler { enum class pipeline { server }; bool is_executing(pipeline) { return fixture::server; } }
namespace vr::h2::sp { int weapon_type(unsigned,bool) { return 1; } }
namespace vr::gameplay::scripted_control { bool firing_allowed(const void*) { return fixture::allowed; } }
namespace vr::gameplay::weapons::cheats {
bool transitioning() { return false; } bool sustain_ammo() { return fixture::sustain; }
}
namespace vr::gameplay::weapons::shield { bool firing_clear(weapon_identity,const vr::gameplay::hands::vec&) { return true; } }
namespace vr::gameplay::weapons::carry { weapon_identity native_identity(unsigned weapon) { return {weapon,weapon}; } }
namespace vr::gameplay::weapons::native_ammunition {
snapshot observe_carried(const void*,weapon_identity id) noexcept { return fixture::clips[id.generation]; }
weapon_identity projected_identity(unsigned weapon) noexcept { return fixture::projected[weapon]; }
bool project(weapon_identity id) noexcept { fixture::projected[id.weapon]=id; return true; }
std::uint64_t timeline() noexcept { return fixture::epoch; }
bool commit_carried(const snapshot& before,int loaded,int reserve) noexcept {
    if(!fixture::commit_ok) return false;
    auto& current=fixture::clips[before.instance_generation]; assert(current==before);
    current.loaded=loaded; current.reserve=reserve; fixture::events.push_back("debit"); return true;
}
}
namespace utils::hook {
template<class Result,class... Args> Result invoke(std::uintptr_t address,Args... args) {
    if constexpr(std::is_same_v<Result,float>) { (void)std::make_tuple(args...); assert(address==native_ballistics::ads_spread_address); return 0.f; }
    else {
        assert(address==native_ballistics::bullet_address);
        auto values=std::make_tuple(args...); const auto* packet=std::get<2>(values);
        assert(std::get<0>(values)==&game::g_entities[0] && std::get<3>(values)==&game::g_entities[0]);
        assert(std::get<4>(values)==1000 && std::get<5>(values)==-1.f);
        assert(packet->shot.origin[0]==1.f && !packet->alternate);
        fixture::events.push_back("bullet"); fixture::bullets.push_back(packet->weapon);
    }
}
}
namespace vr::gameplay::weapons::hei_round {
unsigned projectile_definition(unsigned,bool& requested) noexcept {
    requested=fixture::grenade_mode!=0; return fixture::grenade_mode==2 ? 42u : 0u;
}
void projectile_fired(game::gentity_s*) noexcept {}
}
namespace vr::gameplay::weapons::native_launcher_fire {
bool prepare(unsigned token,const shot_geometry& geometry,native_ballistics::parameters& packet) noexcept {
    assert(token==42);packet={};packet.shot=geometry;packet.weapon=token;return fixture::grenade_prepare;
}
game::gentity_s* fire(game::gentity_s* player,const native_ballistics::parameters& packet) {
    assert(player==&game::g_entities[0] && packet.weapon==42 && packet.shot.origin[0]==1.f);
    fixture::events.push_back("grenade");return player;
}
}
namespace vr::gameplay::weapons::native_ballistics {
namespace {
bool verified=true;
thread_local bool emitting{};
const auto definitions=reinterpret_cast<std::uintptr_t>(fixture::definitions.data());
bool valid(const shot_geometry& g) {
    muzzle_frame muzzle; muzzle.position=g.origin;
    muzzle.axis={g.forward,hands::scale(g.right,-1),g.up}; return valid_geometry(muzzle);
}
}
'''
harness += body
harness += r'''
const shot_geometry ray{{1,0,0},{0,-1,0},{0,0,1},{1,0,0}};
const weapon_identity first{25,1}, second{49,2}, duplicate{25,3}, wide{300,4};
bool settle(const native_ammunition::snapshot& before,const native_ammunition::snapshot& after,bool sustained) noexcept {
    assert(after.loaded==before.loaded-int(!sustained)); fixture::events.push_back("settle");
    if(fixture::on_settle)fixture::on_settle(); return fixture::settle_ok;
}
void reset() {
    using namespace fixture;
    events.clear(); names.clear(); bullets.clear(); clips.clear(); projected={}; definitions.fill(&game::player);
    enabled=allowed=server=commit_ok=settle_ok=true; blocked=sustain=invalid_name=false;
    on_notify={}; on_settle={}; epoch=1; game::player={}; game::g_entities[0].client=&game::player;
    grenade_mode=0;grenade_prepare=true;
    for(auto id:{first,second,duplicate,wide})clips[id.generation]={true,id.weapon,5,30,0,0,id.generation};
}
int main() {
    using namespace fixture; using namespace native_ballistics;
    auto fire=[](weapon_identity id=first){return fire_owned(id,ray,1000,settle);};
    reset(); projected[first.weapon]=duplicate;
    auto result=fire();
    assert(result.status==outcome::emitted && result.notified && result.after.loaded==4);
    assert((events==std::vector<std::string>{"debit","settle","notify","bullet"}));
    assert(names==std::vector<std::string>{"weapon_25_silencer"} && projected[first.weapon]==duplicate);
    // One native command can contain different weapons or two instances of one definition.
    result=fire(second); assert(result.status==outcome::emitted && result.notified);
    result=fire(duplicate); assert(result.status==outcome::emitted && result.notified);
    result=fire(wide); assert(result.status==outcome::emitted && result.notified);
    assert((bullets==std::vector<unsigned>{25,49,25,300}));
    assert((names==std::vector<std::string>{"weapon_25_silencer","weapon_49_silencer","weapon_25_silencer","weapon_300_silencer"}));
    assert(clips[1].loaded==4 && clips[2].loaded==4 && clips[3].loaded==4);
    for(int rejection=0;rejection<5;++rejection) {
        reset();
        if(rejection==0)clips[1].loaded=0;
        if(rejection==1)blocked=true;
        if(rejection==2)commit_ok=false;
        if(rejection==3)server=false;
        if(rejection==4)invalid_name=true;
        result=fire(); assert(result.status!=outcome::emitted && !result.notified);
        assert(names.empty() && bullets.empty() && events.empty());
    }
    reset(); settle_ok=false; result=fire();
    assert(result.status==outcome::mechanical_changed && !result.notified && names.empty() && bullets.empty());
    assert(clips[1].loaded==4); // No refund of an uncertain mechanical commit.
    reset(); sustain=true; result=fire();
    assert(result.status==outcome::emitted && result.notified && clips[1].loaded==5 && names.size()==1);
    for(bool during_notify:{false,true}) {
        reset(); int nested_calls=0;
        const auto nested=[&]{++nested_calls; const auto rejected=fire(second);
            assert(rejected.status==outcome::invalid_context && !rejected.notified);};
        if(during_notify)on_notify=nested; else on_settle=nested;
        result=fire(); assert(result.status==outcome::emitted && nested_calls==1);
        assert(names.size()==1 && bullets.size()==1 && clips[2].loaded==5);
    }
    // Script callbacks may replace a player, roll back time, or invalidate a level.
    for(int change=0;change<4;++change) {
        reset(); on_notify=[&]{if(change==0)game::g_entities[0].client=nullptr;
            if(change==1)++epoch; if(change==2)game::player.commandTime=0; if(change==3)enabled=false;};
        result=fire(); assert(result.status==outcome::context_changed && result.notified);
        assert(names.size()==1 && bullets.empty() && clips[1].loaded==4);
    }
    reset(); result=fire(); assert(result.status==outcome::emitted); // Guard released after rejected/lost contexts.
    reset(); grenade_mode=2;result=fire();
    assert(result.status==outcome::emitted && result.notified && result.after.loaded==4 && bullets.empty());
    assert((events==std::vector<std::string>{"debit","settle","notify","grenade"}));
    assert(names==std::vector<std::string>{"weapon_25_silencer"} && clips[2].loaded==5);
    reset();grenade_mode=1;result=fire();
    assert(result.status==outcome::unsupported_weapon && !result.notified && clips[1].loaded==5 && events.empty());
    reset();grenade_mode=2;grenade_prepare=false;result=fire();
    assert(!result.notified && clips[1].loaded==5 && events.empty());
    reset();grenade_mode=2;sustain=true;result=fire();
    assert(result.status==outcome::emitted && clips[1].loaded==5 && bullets.empty() && events.back()=="grenade");
    reset();grenade_mode=2;on_notify=[]{++epoch;};result=fire();
    assert(result.status==outcome::context_changed && result.notified && events.back()=="notify");
    std::cout << "Production fire_owned: PASS (order, actual weapon, same-tick instances, rejected shots, reentry, lifecycle)\n";
}
'''
(out / 'adapter.cpp').write_text(harness, encoding='utf-8')
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installs = json.loads(subprocess.check_output([str(vswhere), '-all', '-products', '*',
    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json', '-utf8'], encoding='utf-8'))
install = Path(next(x for x in installs if x['installationVersion'].startswith('16.'))['installationPath'])
vcvars = install / 'VC/Auxiliary/Build/vcvars64.bat'
driver = out / 'test.cmd'
driver.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
    f'cl /nologo /std:c++latest /EHsc /W4 /I"{root / "src/client"}" '
    f'/I"{root / "deps/GSL/include"}" adapter.cpp /Fe:adapter.exe\n'
    'if errorlevel 1 exit /b 1\nadapter.exe\n', encoding='utf-8')
subprocess.run(['cmd.exe', '/d', '/c', str(driver)], cwd=out, check=True)
