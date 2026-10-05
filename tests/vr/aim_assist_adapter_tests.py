"""Compile the actual native aim adapter against deterministic query/trace endpoints."""
from pathlib import Path
import json
import os
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / 'src/client/component/vr/gameplay/aim_assist.cpp').read_text(encoding='utf-8')
body = source[source.index('namespace vr::gameplay::aim_assist'):]
out = root / 'build/aim-assist-adapter-tests'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include "component/vr/gameplay/aim_assist.hpp"
#include "component/vr/gameplay/aim_assist_geometry.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>
#include <mutex>
#include <sstream>
using vec = vr::gameplay::hands::vec;
struct row { unsigned entnum; bool alive; vec point; bool valid=true; unsigned classnum=0;
    unsigned team=1; std::string type,classname,targetname; };
std::vector<row> rows;
std::vector<unsigned> enemy_rows;
int list_calls{}, alive_calls{}, point_calls{}, trace_calls{}, fail_after=-1, count_override=-1;
std::string map_name="roadkill", start_point;
bool map_available=true, start_defined=false, flags_defined=true, done_defined=true, query_fail=false;
int opening_done=0, phase_reads=0;
struct trace_result { float fraction=1; int hitType=0; unsigned short hitId=0; bool allsolid=false,startsolid=false; };
std::vector<trace_result> trace_results;
namespace game {
struct dvar_t { struct { const char* string; } current; } map;
unsigned level_id=1; unsigned* levelEntityId=&level_id;
const dvar_t* Dvar_FindVar(const char* name) {
    assert(std::string(name)=="mapname"); map.current.string=map_name.c_str();
    return map_available ? &map : nullptr;
}
using trace_t=trace_result;
struct Bounds {};
void G_TraceCapsule(trace_t* result, const float* start, const float* end, Bounds*, unsigned shooter, int mask) {
    assert(shooter==0 && mask==0x280e831 && start[0]==0 && end[0]>0);
    *result=trace_calls<int(trace_results.size()) ? trace_results[trace_calls] : trace_t{};
    ++trace_calls;
}
}
namespace scripting {
struct vector { vec point; float operator[](unsigned i) const { return point[i]; } };
struct value;
struct array;
struct entity {
    unsigned index;
    struct reference { unsigned entnum, classnum; };
    reference get_entity_reference() const { return {rows[index].entnum,rows[index].classnum}; }
    value call(const std::string&) const;
    value get(const std::string&) const;
};
struct value {
    unsigned index;
    unsigned kind=0;
    template<class T> bool is() const {
        if constexpr(std::is_same_v<T,std::string>) return (kind==1 && start_defined) || kind>=4;
        else if constexpr(std::is_same_v<T,array>) return kind==2 && flags_defined;
        else if constexpr(std::is_same_v<T,int>) return kind==3 && done_defined;
        else return kind==0 && rows[index].valid;
    }
    template<class T> T as() const {
        if constexpr(std::is_same_v<T,entity>) return {index};
        else if constexpr(std::is_same_v<T,std::string>) {
            if(kind==4)return rows[index].type;
            if(kind==5)return rows[index].classname;
            if(kind==6)return rows[index].targetname;
            return start_point;
        }
        else if constexpr(std::is_same_v<T,array>) return {};
        else if constexpr(std::is_same_v<T,int>) return opening_done;
        else return {rows[index].point};
    }
};
value entity::get(const std::string& field) const {
    if(field=="type")return {index,4};
    if(field=="classname")return {index,5};
    if(field=="targetname")return {index,6};
    assert(index==game::level_id); ++phase_reads;
    if(query_fail) throw std::runtime_error("phase query failed");
    if(field=="start_point") return {0,1};
    assert(field=="flag"); return {0,2};
}
value entity::call(const std::string& name) const {
    assert(name=="getshootatpos");
    if (point_calls++==fail_after) throw std::runtime_error("native query failed");
    return {index};
}
struct array {
    int size() const { return count_override==-1 ? int(enemy_rows.size()) : count_override; }
    value get(unsigned i) const { return {enemy_rows.at(i)}; }
    value get(const std::string& key) const { assert(key=="torture_sequence_done"); return {0,3}; }
};
template<class T> T call(const std::string& name, std::initializer_list<const char*> args) {
    assert(name=="getaiarray" && (args.size()==1 || args.size()==2) && std::string(*args.begin())=="bad_guys");
    const bool neutral=args.size()==2;
    if(neutral)assert(std::string(*(args.begin()+1))=="neutral");
    // Audited native parser ORs all arguments: bad_guys=0x0a, neutral=0x10.
    // A combined museum query must still exclude allies rather than use all.
    enemy_rows.clear();for(unsigned i=0;i<rows.size();++i)
        if(rows[i].team==1 || rows[i].team==3 || (neutral && rows[i].team==4))enemy_rows.push_back(i);
    ++list_calls; return {};
}
template<class T> T call(const std::string& name, std::initializer_list<entity> args) {
    assert(name=="isalive" && args.size()==1); ++alive_calls;
    return rows[args.begin()->index].alive;
}
}
'''
harness += body
harness += r'''
int main() {
    using namespace vr::gameplay;
    const weapons::shot_geometry original{{1,0,0},{0,-1,0},{0,0,1},{0,0,0}};
    auto shot=original;
    auto reset=[&] { shot=original; rows.clear(); trace_results.clear(); list_calls=alive_calls=point_calls=trace_calls=0; fail_after=-1; count_override=-1; };
    auto unchanged=[&] { assert(shot.forward==original.forward && shot.origin==original.origin && shot.right==original.right && shot.up==original.up); };
    map_name="favela";
    for(const auto& start : {"", "street", "chase", "favela", "torture"}) {
        reset(); start_point=start; start_defined=!start_point.empty(); rows={{1,true,{100,1,0}}};
        assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::disabled);
        assert(list_calls+alive_calls+point_calls+trace_calls==0); unchanged();
    }
    for(int done : {1,0,1}) {
        reset(); opening_done=done; rows={{1,true,{100,1,0}}};
        assert(aim_assist::apply(shot,100,0)==(done ? aim_assist::outcome::applied : aim_assist::outcome::disabled));
        if(!done) { unchanged(); assert(!list_calls && !trace_calls); }
    }
    opening_done=0;
    for(const auto& start : {"soccer", "hilltop", "trailer1", "trailer2", "trailer3", "end"}) {
        reset(); start_point=start; rows={{1,true,{100,1,0}}};
        assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::applied);
    }
    start_point="chase";
    reset(); flags_defined=false;
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::disabled); unchanged(); flags_defined=true;
    reset(); done_defined=false;
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::disabled); unchanged(); done_defined=true;
    reset(); game::level_id=0;
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::disabled); unchanged(); game::level_id=1;
    reset(); query_fail=true;
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::query_failed); unchanged(); assert(!list_calls);
    query_fail=false;
    reset(); phase_reads=0;
    assert(aim_assist::apply(shot,0,0)==aim_assist::outcome::disabled && phase_reads==0);
    map_name="favela_escape";
    reset(); rows={{1,true,{100,1,0}}}; phase_reads=0;
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::applied && phase_reads==0);
    map_name="roadkill"; start_defined=false;
    for(float strength : {0.f,-1.f,std::numeric_limits<float>::quiet_NaN()}) {
        reset(); assert(aim_assist::apply(shot,strength,0)==aim_assist::outcome::disabled);
        assert(list_calls+alive_calls+point_calls+trace_calls==0); unchanged();
    }
    reset(); assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::no_target); unchanged();
    reset(); rows={{1,false,{100,1,0}},{0,true,{100,1,0}},{4000,true,{100,1,0}},{4,true,{100,1,0},false},{5,true,{100,1,0},true,1}};
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::no_target);
    assert(alive_calls==1 && point_calls==0 && trace_calls==0); unchanged();
    for(auto blocked : {trace_result{.5f,1,9},trace_result{.5f,2,1},trace_result{.5f,1,1,true,false},
                        trace_result{.5f,1,1,false,true},trace_result{-1},trace_result{1.1f},
                        trace_result{std::numeric_limits<float>::quiet_NaN()}}) {
        reset(); rows={{1,true,{100,2,0}}}; trace_results={blocked};
        assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::no_target); unchanged();
    }
    reset(); rows={{1,true,{100,2,0}}}; trace_results={{.9f,1,1}};
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::applied && shot.forward[1]>0 && shot.origin==original.origin);
    reset(); rows={{1,true,{100,1,0}},{2,true,{100,3,0}}}; trace_results={{.5f,1,9},{1}};
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::applied && shot.forward[1]>.029f);
    reset(); rows={{1,true,{100,2,0}},{2,true,{100,1,0}}}; fail_after=1;
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::query_failed); unchanged();
    reset(); count_override=4001;
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::query_failed && alive_calls==0); unchanged();
    reset(); rows={{1,true,{100,2,0}}};
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::applied);
    shot=original; rows.clear();
    assert(aim_assist::apply(shot,100,0)==aim_assist::outcome::no_target); unchanged();

    // Independent shots must exercise the same adapter and shared accounting.
    reset(); rows={{3000,true,{100,2,0}}};
    assert(aim_assist::apply(shot,100,0,aim_assist::shot_route::independent)==aim_assist::outcome::applied);
    assert(shot.forward[1]>0 && shot.origin==original.origin);
    auto report=aim_assist::status();
    assert(report.find("assist_route=independent applied=1 no_target=0 errors=0 disabled=0")!=std::string::npos);
    assert(report.find("last_target=3000")!=std::string::npos);
    reset(); assert(aim_assist::apply(shot,100,0,aim_assist::shot_route::independent)==aim_assist::outcome::no_target);
    unchanged(); report=aim_assist::status();
    assert(report.find("assist_route=independent applied=1 no_target=1")!=std::string::npos);
    assert(report.find("state=no hostile AI")!=std::string::npos);
    reset(); rows={{3000,true,{100,2,0}}}; fail_after=0;
    assert(aim_assist::apply(shot,100,0,aim_assist::shot_route::independent)==aim_assist::outcome::query_failed);
    unchanged(); report=aim_assist::status();
    assert(report.find("assist_route=independent applied=1 no_target=1 errors=1")!=std::string::npos);
    assert(report.find("state=native target query failed")!=std::string::npos);

    for(auto route:{aim_assist::shot_route::projected,aim_assist::shot_route::independent}) {
        map_name="roadkill";
        for(unsigned team:{2u,4u}) {
            reset(); rows={{1,true,{100,1,0},true,0,team}};
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::no_target);
            assert(alive_calls==0 && point_calls==0 && trace_calls==0); unchanged();
        }
        for(unsigned team:{1u,3u}) {
            reset(); rows={{1,true,{100,2,0},true,0,team}};
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::applied);
        }
        for(const auto& map:{"ending","museum","roadkill","ending_extra","museum_extra","","museum","favela_escape","ending"}) {
            map_name=map;
            const bool museum=map_name=="ending" || map_name=="museum";
            reset();rows={{1,true,{100,2,0},true,0,4},{2,true,{100,.1f,0},true,0,2}};
            assert(aim_assist::apply(shot,100,0,route)==(museum ? aim_assist::outcome::applied : aim_assist::outcome::no_target));
            assert(list_calls==1);
            if(museum) {
                assert(shot.forward[1]>.019f && trace_calls==1); // Neutral wins; closer allied aim is excluded.
                assert(aim_assist::status().find("last_neutral_enabled=1")!=std::string::npos);
            } else {assert(point_calls==0 && trace_calls==0);unchanged();}
        }
        map_name="ending";map_available=false;
        reset();rows={{1,true,{100,2,0},true,0,4}};
        assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::no_target);unchanged();map_available=true;
        for(const auto& map:{"ending","museum"}) {
            map_name=map;
            reset();rows={{1,true,{100,2,0},true,0,4}};
            assert(aim_assist::apply(shot,0,0,route)==aim_assist::outcome::disabled && list_calls==0);unchanged();
            for(const vec point:{vec{100,30,0},vec{1301,0,0}}) {
                reset();rows={{1,true,point,true,0,4}};
                assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::no_target && trace_calls==0);unchanged();
            }
            reset();rows={{1,true,{100,2,0},true,0,4}};trace_results={{.5f,1,9}};
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::no_target);unchanged();
            reset();rows={{1,true,{100,2,0},true,0,4,"civilian"}};
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::no_target && trace_calls==0);unchanged();
            reset();rows={{1,true,{100,3,0},true,0,4},{2,true,{100,1,0}}};
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::applied && shot.forward[1]<.011f);
            reset();rows={{1,true,{100,1,0},true,0,4},{2,true,{100,3,0}}};
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::applied && shot.forward[1]<.011f);
            reset();rows={{1,true,{100,1,0},true,0,4},{2,true,{100,3,0}}};fail_after=1;
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::query_failed);unchanged();
        }
        // Civilian identity remains protected even if a script changes its team.
        for(unsigned marker=0;marker<3;++marker) {
            reset(); rows={{1,true,{100,1,0}},{2,true,{100,3,0}}};
            if(marker==0)rows[0].type="civilian";
            if(marker==1)rows[0].classname="actor_civilian_favela";
            if(marker==2)rows[0].targetname="upperdeck_canned_deaths_drone";
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::applied);
            assert(shot.forward[1]>.029f && trace_calls==1);
            assert(aim_assist::status().find("last_protected_targets=1")!=std::string::npos);
            shot=original; rows.resize(1); trace_calls=0;
            assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::no_target);
            assert(trace_calls==0); unchanged();
        }
        map_name="favela";start_point="chase";start_defined=true;opening_done=0;
        reset();rows={{1,true,{100,1,0}}};
        assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::disabled);
        assert(list_calls==0 && trace_calls==0);unchanged();
        opening_done=1;
        assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::applied);
        shot=original;rows[0].team=4;
        assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::no_target);unchanged();rows[0].team=1;
        shot=original;opening_done=0;
        assert(aim_assist::apply(shot,100,0,route)==aim_assist::outcome::disabled);unchanged();
    }

    std::cout << "Actual aim adapter: PASS (museum neutral scope/transitions, native teams, civilians, campaign rollback, cover, failure atomicity, H2 entity range, both shot routes and diagnostics)\n";
}
'''
(out / 'adapter.cpp').write_text(harness, encoding='utf-8')
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installations = json.loads(subprocess.check_output([str(vswhere), '-all', '-products', '*',
    '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json', '-utf8'], encoding='utf-8'))
install = Path(max(installations, key=lambda x: tuple(map(int,x['installationVersion'].split('.'))))['installationPath'])
vcvars = install / 'VC/Auxiliary/Build/vcvars64.bat'
driver = out / 'test.cmd'
driver.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
    f'cl /nologo /std:c++latest /EHsc /W4 /I"{root / "src/client"}" adapter.cpp /Fe:adapter.exe\n'
    'if errorlevel 1 exit /b 1\nadapter.exe\n', encoding='utf-8')
subprocess.run(['cmd.exe', '/d', '/c', str(driver)], cwd=out, check=True)
