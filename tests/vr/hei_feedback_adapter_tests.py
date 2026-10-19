"""Exercise the production HEI consumer and sound helper with engine endpoints replaced."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
gameplay = root / 'src/client/component/vr/gameplay'


def function(file, signature):
    text = (gameplay / file).read_text(encoding='utf-8')
    start = text.index(signature)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


sound = function('native_weapon_sound.cpp', 'bool play_explosive_impact(')
sound = sound.replace('*reinterpret_cast<const signed char*>(0x141BB3C30)', 'fixture::entity')
tick = function('hei_round.cpp', 'void tick()')
out = root / 'build/hei-feedback-adapter-tests'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <string_view>
#include <vector>
using point = std::array<float,3>;
namespace fixture {
bool main_thread=true, initialized=true, enabled=true, fx_present=true, default_fx=false;
bool sound_present=true, default_sound=false, sound_accept=true, fx_accept=true;
signed char entity=0;
int now=1000;
std::vector<point> audio, visuals;
}
namespace scheduler {
enum class pipeline { main };
bool is_executing(pipeline) { return fixture::main_thread; }
}
namespace game {
enum { ASSET_TYPE_SOUND, ASSET_TYPE_FX };
struct snd_alias_list_t { const char* aliasName="bullet_explode_default"; void* head=this; unsigned count=1; } alias;
struct asset { snd_alias_list_t* sound; void* fx; };
struct dvar_t { struct { int integer=0; } current; } paused;
int catcher=0; int* keyCatchers=&catcher;
bool CL_IsCgameInitialized() { return fixture::initialized; }
const dvar_t* Dvar_FindVar(const char* name) { assert(std::string_view(name)=="cl_paused"); return &paused; }
int CG_GetGameTime(int client) { assert(client==0); return fixture::now; }
bool DB_XAssetExists(int type,const char*) { assert(type==ASSET_TYPE_FX); return fixture::fx_present; }
bool DB_IsXAssetDefault(int type,const char*) { return type==ASSET_TYPE_SOUND ? fixture::default_sound : fixture::default_fx; }
asset DB_FindXAssetHeader(int type,const char* name,int allow_default) {
    assert(!allow_default);
    if(type==ASSET_TYPE_SOUND) {
        assert(std::string_view(name)=="bullet_explode_default");
        return {fixture::sound_present ? &alias : nullptr,nullptr};
    }
    return {nullptr,fixture::fx_present ? &alias : nullptr};
}
}
namespace utils::hook {
template<class Result> Result invoke(std::uintptr_t address,short entity,const float* origin,const char* name) {
    assert(address==0x140380430 && entity==fixture::entity);
    assert(std::string_view(name)=="bullet_explode_default");
    fixture::audio.push_back({origin[0],origin[1],origin[2]});
    return fixture::sound_accept ? 1 : -1;
}
}
namespace vr::gameplay::weapons::native_weapon_sound {
bool verified=true;
constexpr std::uintptr_t play_address=0x140380430;
'''
harness += sound + r'''
}
namespace vr::gameplay::weapons {
namespace hands { struct anchor { point position; std::array<float,4> rotation; }; }
namespace native_weapon_fx {
bool play_frontend(void* fx,const hands::anchor& pose,int time) {
    assert((fx && time==1000 && pose.rotation==std::array<float,4>({0,0,0,1})));
    fixture::visuals.push_back(pose.position); return fixture::fx_accept;
}
}
namespace hei_round {
struct feedback { point position; int time; std::uint64_t epoch,serial; };
std::array<feedback,128> queue;
std::size_t count{};
std::mutex mutex;
std::atomic_uint64_t generation{1},sounds{},sound_rejections{},effects{},discarded{};
bool enabled() { return fixture::enabled; }
'''
harness += tick + r'''
}
}
int main() {
    using namespace fixture;
    using namespace vr::gameplay::weapons;
    using namespace hei_round;
    const point contact{21,32,43};
    auto enqueue=[&]{queue[count++]={contact,1000,1,1};};
    enqueue(); tick(); tick();
    assert(audio==std::vector<point>{contact} && visuals==audio && sounds==1 && effects==1);
    // Independent resources: each available feedback still runs once.
    fx_present=false; enqueue(); tick(); tick();
    assert(audio.size()==2 && visuals.size()==1 && discarded==1);
    fx_present=true; sound_present=false; enqueue(); tick(); tick();
    assert(audio.size()==2 && visuals.size()==2 && sound_rejections==1);
    sound_present=true;
    // Pause/menu/disable/unload/time discontinuities consume without delayed replay.
    for(int kind=0;kind<7;++kind) {
        enqueue();
        if(kind==0)game::paused.current.integer=1;
        if(kind==1)game::catcher=1;
        if(kind==2)fixture::enabled=false;
        if(kind==3)initialized=false;
        if(kind==4)++generation;
        if(kind==5)now=999;
        if(kind==6)now=1251;
        tick();
        game::paused.current.integer=game::catcher=0; fixture::enabled=initialized=true; generation=1; now=1000;
        tick(); assert(audio.size()==2 && visuals.size()==2 && count==0);
    }
    // Current-zone sound validation and the owner-thread/position contract.
    using native_weapon_sound::play_explosive_impact;
    for(int kind=0;kind<9;++kind) {
        point position=contact;
        if(kind==0)default_sound=true;
        if(kind==1)game::alias.count=0;
        if(kind==2)game::alias.head=nullptr;
        if(kind==3)game::alias.aliasName="different_alias";
        if(kind==4)entity=-1;
        if(kind==5)main_thread=false;
        if(kind==6)native_weapon_sound::verified=false;
        if(kind==7)position[0]=std::numeric_limits<float>::quiet_NaN();
        if(kind==8)position[1]=1.e8f;
        assert(!play_explosive_impact(position) && audio.size()==2);
        default_sound=false; game::alias={}; entity=0; main_thread=native_weapon_sound::verified=true;
    }
    sound_accept=false; enqueue(); tick(); sound_accept=true; tick();
    assert(audio.size()==3 && sound_rejections==2); // Native rejection is never retried.
    std::cout << "Production HEI feedback: PASS (position, single playback, resource independence, lifecycle, alias guards)\n";
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
