"""Exercise the production reserve loop against native trainer pickup boundaries."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / 'src/client/component/vr/gameplay/launcher_cheats.cpp').read_text(encoding='utf-8')
start = source.index('void refill_reserves(')
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body = source[start:end]
out = root / 'build/launcher-reserve-adapter-tests'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
namespace game {
enum weapInventoryType_t { WEAPINVENTORY_PRIMARY, WEAPINVENTORY_OFFHAND,
    WEAPINVENTORY_ITEM, WEAPINVENTORY_ALTMODE, WEAPINVENTORY_EXCLUSIVE, WEAPINVENTORY_SCAVENGER };
struct WeaponDef { weapInventoryType_t inventoryType; };
std::array<WeaponDef*,512> weapon_defs{};
}
namespace utils::native_memory {
bool read_bytes(void* out,const void* in,std::size_t size) {
    if(reinterpret_cast<std::uintptr_t>(in)<0x10000)return false;
    std::memcpy(out,in,size);return true;
}
template<class T>bool read_at(const void* in,std::size_t at,T& out) {
    return in && read_bytes(&out,static_cast<const std::byte*>(in)+at,sizeof(out));
}
}
namespace vr::gameplay::weapons {
namespace native_ammunition {
struct snapshot { bool valid{};unsigned weapon{};int loaded{},reserve{}; };
std::array<snapshot,512> ammo{};
snapshot observe_carried(const void*,unsigned token) { return token<512 ? ammo[token] : snapshot{}; }
bool commit_carried(snapshot before,int loaded,int reserve) {
    auto& current=ammo[before.weapon];
    if(current.loaded!=before.loaded || current.reserve!=before.reserve)return false;
    current.loaded=loaded;current.reserve=reserve;return true;
}
unsigned projected_identity(unsigned token) {return token;}
}
namespace underbarrel::native {
unsigned resolve(unsigned token) { return token==9 ? token : 0; }
struct observation {bool valid{};unsigned host{};struct {int loaded{},reserve{};} ammo;};
observation secondary{true,9,{1,0}};
observation observe(unsigned token) {return token==9 ? secondary : observation{};}
struct counts {int loaded,reserve;};
bool commit(observation before,counts after) {
    if(before.ammo.loaded!=secondary.ammo.loaded || before.ammo.reserve!=secondary.ammo.reserve)return false;
    secondary.ammo={after.loaded,after.reserve};return true;
}
}
}
namespace vr::gameplay::cheats::launcher {
constexpr int reserve_rounds=500;
std::uint64_t reserve_writes{},reserve_rejections{};
'''
harness += body + r'''
}
int main() {
    namespace a=vr::gameplay::weapons::native_ammunition;
    namespace u=vr::gameplay::weapons::underbarrel::native;
    namespace l=vr::gameplay::cheats::launcher;
    int failures{};
    auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<"FAIL: "<<why<<'\n';}};
    std::array<std::byte,0x400> player{};
    // Live trainer fixture: m4_grunt_ssdd=9 and fraggrenade=22. Other classes
    // represent non-carried mission equipment, never a generic ammunition grant.
    std::array<unsigned,15> inventory{9,22,23,24,25,26,0,512,0xffffffff,27};
    std::memcpy(player.data()+0x2f8,inventory.data(),sizeof(inventory));
    std::array<game::WeaponDef,6> defs{{{game::WEAPINVENTORY_PRIMARY},
        {game::WEAPINVENTORY_OFFHAND},{game::WEAPINVENTORY_OFFHAND},
        {game::WEAPINVENTORY_ITEM},{game::WEAPINVENTORY_ALTMODE},{game::WEAPINVENTORY_EXCLUSIVE}}};
    for(unsigned i=0;i<defs.size();++i) {
        unsigned token=i ? 21+i : 9;game::weapon_defs[token]=&defs[i];a::ammo[token]={true,token,i ? 0 : 30,0};
    }
    a::ammo[27]={true,27,0,0}; // Missing definition must not acquire a reserve.
    for(int frag:{0,2,3,4,0}) {
        a::ammo[22].reserve=frag; // Native reset, pre-pickup, pickup grant, lesson cleanup.
        for(int frame=0;frame<3;++frame)l::refill_reserves(player.data());
        check(a::ammo[22].reserve==frag,"reserve cheat preserves native frag pickup/reset counts");
        check((a::ammo[22].reserve>3)==(frag>3),"native trainer pickup admission is unchanged");
        check((a::ammo[22].reserve<3)==(frag<3),"native trainer lesson wait is unchanged");
        check(a::ammo[9].loaded==30 && a::ammo[9].reserve==500,"primary reserve refills without changing the clip");
        check(u::secondary.ammo.loaded==1 && u::secondary.ammo.reserve==500,"admitted underbarrel retains its independent reserve refill");
    }
    for(unsigned token:{23,24,25,26,27})check(a::ammo[token].reserve==0,"offhand/mission/alternate/invalid entries cannot gain ammo");
    const auto writes=l::reserve_writes;
    a::ammo[9].loaded=29;a::ammo[9].reserve=470;
    u::secondary.ammo={0,499};l::refill_reserves(player.data());
    check(a::ammo[9].loaded==29 && a::ammo[9].reserve==500 &&
        u::secondary.ammo.loaded==0 && u::secondary.ammo.reserve==500,"refill preserves changed loaded counts");
    check(l::reserve_writes==writes+2 && !l::reserve_rejections,"only changed primary/secondary counts are committed");
    l::refill_reserves(player.data());check(l::reserve_writes==writes+2,"unchanged reserves do not write again");
    std::cout<<"Production launcher reserve: "<<(failures ? "FAIL" : "PASS")<<"\n";
    return failures ? 1 : 0;
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
    'cl /nologo /std:c++latest /EHsc /W4 adapter.cpp /Fe:adapter.exe\n'
    'if errorlevel 1 exit /b 1\nadapter.exe\n', encoding='utf-8')
subprocess.run(['cmd.exe', '/d', '/c', str(driver)], cwd=out, check=True)
