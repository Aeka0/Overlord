"""Replay pellet and cone damage through the production physical shield adapter."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / 'src/client/component/vr/gameplay/native_shield.cpp').read_text(encoding='utf-8')
body = source[source.index('  bool angle_query('):source.index('  template<std::size_t N> bool verify(')]
out = root / 'build/native-shield-adapter-tests'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include "component/vr/gameplay/shield_trace.hpp"
#include "component/vr/gameplay/weapon_profile.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"
#include "component/vr/gameplay/weapon_scene.hpp"
#include "component/vr/gameplay/weapons/riot_shield/geometry.hpp"
#include <gsl/gsl>
#include <atomic>
#include <cassert>
#include <iostream>
#include <tuple>
using namespace std::chrono_literals;
using vr::hand;
using namespace vr::gameplay::weapons;
using namespace vr::gameplay::hands;
namespace fixture {
bool managed=true, readable=true, native_angle=true;
int native_damage{}, native_process{}, native_angles{}, effects{}, haptics{};
float native_fraction=1;
vr::controller_input::frame input;
vr::head_pose_bridge::status tracking;
std::array<carry::instance,2> held;
carry::scene scene;
profile authored{};
vec contact_point{};
unsigned event_weapon{};
bool event_alternate{};
}
namespace game {
union Weapon { unsigned data; };
struct gentity_s { int id; } g_entities[2]{{0},{17}};
}
namespace vr::controller_input { frame latest() noexcept { return fixture::input; } }
namespace vr::head_pose_bridge { status get_status() noexcept { return fixture::tracking; } }
namespace vr::gameplay::weapons::carry {
std::array<instance,2> held_instances() noexcept { return fixture::held; }
scene firing_scene(identity id) noexcept { assert(id==fixture::scene.owner.id()); return fixture::scene; }
}
namespace vr::gameplay::weapons::feedback {
void carry_confirmation(hand owner,const vr::controller_input::frame&) noexcept {
    assert(owner==fixture::scene.owner.holding_hand()); ++fixture::haptics;
}
}
namespace utils::native_memory {
bool read_bytes(void* to,const void* from,std::size_t size) {
    if(!fixture::readable)return false;
    std::memcpy(to,from,size); return true;
}
}
namespace utils::hook {
struct detour {
    template<class Result,class... Args> Result invoke(Args...) {
        ++fixture::native_process; return true;
    }
};
template<class Result,class... Args> Result invoke(std::uintptr_t address,Args... args) {
    auto values=std::make_tuple(args...);
    if constexpr(std::is_same_v<Result,bool>) {
        assert(address==0x1404a9b00); ++fixture::native_angles; return fixture::native_angle;
    } else if constexpr(sizeof...(Args)==14) {
        assert(address==0x1404bd2e0);
        assert(std::get<5>(values)==37 && std::get<6>(values)==5);
        assert(std::get<8>(values).data==72 && std::get<9>(values));
        assert(std::get<10>(values)==11 && std::get<11>(values)==2);
        assert(std::get<12>(values)==3 && std::get<13>(values)==4);
        ++fixture::native_damage;
    } else {
        static_assert(sizeof...(Args)==9);
        assert(address==0x1404ac950 && std::get<5>(values)==17);
        assert(std::get<6>(values)==&game::g_entities[0] && std::get<7>(values)==19 && std::get<8>(values)==0x47);
        std::copy_n(std::get<0>(values),3,fixture::contact_point.begin());
        fixture::event_weapon=std::get<3>(values); fixture::event_alternate=std::get<4>(values);
        ++fixture::effects;
    }
}
}
namespace vr::gameplay::weapons::shield {
utils::hook::detour damage_hook;
std::atomic_uint64_t queries{},blocks{},events{},stale{},cone_blocks{};
thread_local bool filtering{},filtering_angles{};
thread_local trace_stamps stamps;
bool manages_player() noexcept { return fixture::managed; }
template<class T> bool read(const void* p,std::size_t offset,T& value) noexcept {
    return p && utils::native_memory::read_bytes(&value,static_cast<const std::byte*>(p)+offset,sizeof(value));
}
'''
harness += body
harness += r'''
}
using namespace vr::gameplay::weapons::shield;
void reset() {
    using namespace fixture;
    managed=readable=native_angle=true;
    native_damage=native_process=native_angles=effects=haptics=0;
    native_fraction=1; queries=blocks=events=stale=cone_blocks=0; stamps={};
    input={};input.sequence=10;input.reference_generation=5;
    input.sampled_at=vr::controller_input::clock::now();input.focused=true;
    for(auto* poses:{&input.grip,&input.aim})for(auto& pose:*poses)
        {pose.valid=true;pose.quality={true,true,true,true,true};}
    tracking={};tracking.enabled=tracking.pose_available=true;tracking.recenter_count=5;
    authored={};authored.defense=&riot_shield::defense;
    scene={};scene.owner={45,3,hand::left,hand::none};scene.owner.instance_generation=7;
    scene.authored=&authored;scene.sequence=10;scene.reference=5;scene.at=input.sampled_at;
    held={};held[0]={scene.owner.id(),{},carry::location::held,scene.owner};
}
bullet_parameters ray(vec start={100,0,0},vec end={-100,0,0}) {
    bullet_parameters p{};p.shooter=17;p.means=1;p.start=start;p.end=end;p.direction=sub(end,start);return p;
}
void cone(bullet_parameters& p,game::gentity_s* target=&game::g_entities[0],const float* point=nullptr) {
    cone_damage(target,&game::g_entities[1],&game::g_entities[1],p.direction.data(),point?point:p.end.data(),
        37,5,p.means,game::Weapon{72},true,11,2,3,4);
}
bool native_trace(bullet_parameters*,unsigned,bool,game::gentity_s*,bullet_result* result,int) {
    result->fraction=fixture::native_fraction;return result->fraction<1;
}
int main() {
    using namespace fixture;
    // Original defect: blocking visual pellets does not cancel the separate native cone damage.
    reset();auto p=ray();bullet_result result{};
    assert(trace(native_trace,&p,72,true,&game::g_entities[1],&result,0));
    int flags=9;
    assert(!damage(nullptr,&p,&result,72,true,0,&game::g_entities[1],0,0,&flags,true));
    assert(blocks==1 && flags==0 && effects==1);
    utils::hook::invoke<void>(0x1404bd2e0,&game::g_entities[0],&game::g_entities[1],&game::g_entities[1],
        p.direction.data(),p.end.data(),37,5,1u,game::Weapon{72},true,11u,2,3u,4u);
    assert(native_damage==1); // The old unhooked cone call still hurts the player.
    reset();p=ray();cone(p);
    assert(native_damage==0 && cone_blocks==1 && blocks==1 && effects==1 && haptics==1);
    assert(contact_point[0]>2 && event_weapon==72 && event_alternate);
    // Window coverage is physical; exposed body, rear fire and a segment ending early stay native.
    for(auto endpoints:{std::array<vec,2>{{{100,0,14},{-100,0,14}}},
                        std::array<vec,2>{{{100,30,0},{-100,30,0}}},
                        std::array<vec,2>{{{-100,0,0},{100,0,0}}},
                        std::array<vec,2>{{{100,0,0},{50,0,0}}}}) {
        reset();p=ray(endpoints[0],endpoints[1]);cone(p);
        assert(native_damage==(endpoints[0][2]==14?0:1));
    }
    // Native wall ordering still wins on pellet traces; the cone caller owns its sight check.
    reset();p=ray();native_fraction=.1f;
    assert(trace(native_trace,&p,72,false,&game::g_entities[1],&result,0) && result.fraction==.1f);
    assert(damage(nullptr,&p,&result,72,false,0,&game::g_entities[1],0,0,nullptr,true));
    assert(native_process==1 && effects==0 && blocks==0);
    // Every unavailable ownership/tracking context falls through without granting protection.
    for(int rejection=0;rejection<13;++rejection) {
        reset();p=ray();
        if(rejection==0)managed=false;
        if(rejection==1)held={};
        if(rejection==2)scene.authored=nullptr;
        if(rejection==3)scene.at-=151ms;
        if(rejection==4)++scene.owner.revision;
        if(rejection==5)++scene.reference;
        if(rejection==6)input.focused=false;
        if(rejection==7)input.aim[0].valid=false;
        if(rejection==8)tracking.recenter_pending=true;
        if(rejection==9)readable=false;
        if(rejection==10)p.means=3;
        if(rejection==11)p.means=8;
        if(rejection==12)input.sampled_at-=151ms;
        cone(p);assert(native_damage==1 && effects==0 && cone_blocks==0);
    }
    reset();p=ray();cone(p,&game::g_entities[1]);assert(native_damage==1 && queries==0);
    reset();p=ray();auto separate_point=p.end;cone(p,&game::g_entities[0],separate_point.data());
    assert(native_damage==1 && queries==0); // A changed native packet contract cannot grant immunity.
    reset();p=ray();
    assert(!cone_angle_query(&game::g_entities[0],&game::g_entities[1],p.start.data(),p.end.data()));
    assert(native_angles==0); // Offhand physical shield does not borrow selected-weapon facing protection.
    assert(cone_angle_query(&game::g_entities[1],&game::g_entities[0],p.start.data(),p.end.data()));
    managed=false;assert(cone_angle_query(&game::g_entities[0],&game::g_entities[1],p.start.data(),p.end.data()));
    assert(native_angles==2); // NPC and disabled-VR angle policies remain native.
    std::cout << "Production shield adapter: PASS (pellet/cone bypass, native forwarding, finite panels, tracking, NPCs)\n";
}
'''
(out / 'adapter.cpp').write_text(harness, encoding='utf-8')
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installs = json.loads(subprocess.check_output([str(vswhere), '-all', '-products', '*',
    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json', '-utf8'], encoding='utf-8'))
install = Path(next(x for x in installs if x['installationVersion'].startswith('16.'))['installationPath'])
driver = out / 'test.cmd'
driver.write_text(f'@echo off\ncall "{install / "VC/Auxiliary/Build/vcvars64.bat"}" >nul\n'
    'if errorlevel 1 exit /b 1\n'
    f'cl /nologo /std:c++latest /EHsc /W4 /I"{root / "src/client"}" '
    f'/I"{root / "deps/GSL/include"}" adapter.cpp /Fe:adapter.exe\n'
    'if errorlevel 1 exit /b 1\nadapter.exe\n', encoding='utf-8')
subprocess.run(['cmd.exe', '/d', '/c', str(driver)], cwd=out, check=True)
