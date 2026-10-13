"""Run production Lua restart/retirement code with real Lua/sol references.

Native game entry points and script discovery are replaced at the boundary.
No game is launched and no installed scripts or profiles are accessed.
"""
from pathlib import Path
import json
import os
import subprocess


root = Path(__file__).resolve().parents[1]
engine = (root / "src/client/game/scripting/lua/engine.cpp").read_text(encoding="utf-8")
notifies = (root / "src/client/component/notifies.cpp").read_text(encoding="utf-8")


def function(signature, indent="\t"):
    start = notifies.index(indent + signature)
    opening = notifies.index("\n" + indent + "{", start) + len(indent) + 1
    end, depth = opening + 1, 1
    while depth:
        depth += (notifies[end] == "{") - (notifies[end] == "}")
        end += 1
    return notifies[start:end]


harness = r'''
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << std::endl; std::_Exit(1); }
}
namespace game { struct gentity_s {}; }
namespace notifies {
    using gsc_hook_guard = bool(*)();
    using entity_damage_filter = int(*)(const game::gentity_s*, const game::gentity_s*, int, unsigned int);
'''
harness += notifies[notifies.index("\t\tstruct gsc_hook_t"):notifies.index("\t\tutils::hook::detour")]
harness += notifies[notifies.index("\t\tstd::vector<sol::protected_function>"):notifies.index("\t\tchar empty_function")]
harness += r'''
    char program[32]{};
    bool gsc_hooks_buffer[32]{};
    size_t get_program_buffer_offset(const char* pos) { return pos - program; }
'''
harness += "\n".join(function(name, indent) for name, indent in (
    ("void set_hook(", "\t\t"),
    ("void add_entity_damage_callback(", "\t"),
    ("void add_entity_damage_filter(", "\t"),
    ("void clear_callbacks()", "\t"),
    ("void set_lua_hook(", "\t"),
    ("void set_gsc_hook(", "\t"),
))
harness += r'''
}
namespace filesystem { std::vector<std::string> get_search_paths_rev() { return {}; } }
namespace utils::io {
    bool directory_exists(const std::string&) { return false; }
    bool file_exists(const std::string&) { return false; }
    std::vector<std::string> list_files(const std::string&) { return {}; }
}
namespace scripting::lua {
    struct event {};
    struct damage_entity { int health; };
    int generation{}, closed{};
    struct context {
        sol::state state;
        context() {
            state.open_libraries(sol::lib::base);
            state.new_usertype<damage_entity>("entity", "health", &damage_entity::health);
            state["generation"] = ++generation;
            state.script("function damage(entity) return generation + entity.health end");
            sol::protected_function callback = state["damage"];
            notifies::add_entity_damage_callback(callback);
            notifies::set_lua_hook(notifies::program + 1, callback, false);
            notifies::set_lua_hook(notifies::program + 2, callback, true);
        }
        explicit context(const std::string&) : context() {}
        ~context() {
            // Fail before lua_close makes the dangling references unsafe to destroy.
            for (const auto& callback : notifies::entity_damage_callbacks)
                require(callback.lua_state() != state.lua_state(), "damage callback outlived its Lua context");
            for (const auto& entry : notifies::vm_execute_hooks)
                require(!entry.second.is_lua_hook || entry.second.lua_function.lua_state() != state.lua_state(),
                        "Lua hook outlived its Lua context");
            ++closed;
        }
        void notify(const event&) {}
        void handle_endon_conditions(const event&) {}
        void run_frame() { state.collect_garbage(); }
        std::string load(const std::string&) { return {}; }
    };
    namespace engine { void stop(); }
}
'''
harness += engine[engine.index("namespace scripting::lua::engine"):]
harness += r'''
int native_filter(const game::gentity_s*, const game::gentity_s*, int damage, unsigned int) { return damage; }
bool native_guard() { return true; }
void check_native() {
    require(notifies::entity_damage_filters.size() == 1 && notifies::entity_damage_filters[0] == native_filter,
            "Lua retirement removed persistent native damage filter");
    const auto& hook = notifies::vm_execute_hooks.at(notifies::program + 3);
    require(!hook.is_lua_hook && hook.target_pos == notifies::program + 4 && hook.guard == native_guard &&
            notifies::gsc_hooks_buffer[3], "Lua retirement changed native GSC redirect");
}
int main() {
    using namespace scripting::lua;
    notifies::add_entity_damage_filter(native_filter);
    notifies::set_gsc_hook(notifies::program + 3, notifies::program + 4, native_guard);
    engine::stop(); // Empty startup and repeated stop must both be harmless.
    for (int cycle = 1; cycle <= 16; ++cycle) {
        engine::start(); // Deliberately no preceding stop: spawn/save-load restart.
        require(generation == cycle && closed == cycle - 1, "restart did not replace exactly one context");
        require(notifies::entity_damage_callbacks.size() == 1, "restart accumulated damage callbacks");
        require(notifies::vm_execute_hooks.size() == 3, "restart lost or accumulated hooks");
        check_native();
        engine::run_frame();
        // Exercise real Lua userdata allocation and the new generation's callback.
        const auto& callback = notifies::entity_damage_callbacks.front();
        const auto entity = sol::make_object(callback.lua_state(), damage_entity{42});
        const auto result = callback(entity);
        require(result.valid() && result.get<int>() == cycle + 42, "damage delivered to wrong Lua generation");
    }
    engine::stop();
    engine::stop();
    require(closed == generation && notifies::entity_damage_callbacks.empty() &&
            notifies::vm_execute_hooks.size() == 1 && !notifies::gsc_hooks_buffer[1],
            "stop retained Lua references or hook admission bit");
    check_native();
    engine::start(); // Clean restart after shutdown also remains supported.
    engine::stop();
    require(closed == generation, "final context was not destroyed");
    std::cout << "lua-restart-tests: PASS\n";
}
'''

out = root / "build/lua-restart-tests"
out.mkdir(parents=True, exist_ok=True)
(out / "lua_restart_tests.cpp").write_text(harness, encoding="utf-8")
vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
installations = json.loads(subprocess.check_output([
    str(vswhere), "-latest", "-products", "*", "-requires",
    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-format", "json", "-utf8"
], encoding="utf-8"))
if not installations:
    raise RuntimeError("MSVC C++ build tools are required")
vcvars = Path(installations[0]["installationPath"]) / "VC/Auxiliary/Build/vcvars64.bat"
for configuration, options in (("Debug", "/Od /Zi /MTd"), ("RelWithDebInfo", "/O2 /DNDEBUG /MT")):
    library = root / f"build/bin/x64/{configuration}/lua.lib"
    if not library.is_file():
        raise FileNotFoundError(f"Build the {configuration} lua project first: {library}")
    driver = out / (configuration + ".cmd")
    driver.write_text(
        f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\n'
        f'cl /nologo /std:c++17 /EHsc {options} /utf-8 '
        f'/I"{root / "deps/sol2/include"}" /I"{root / "deps/lua"}" '
        f'lua_restart_tests.cpp "{library}" /Fo:{configuration}.obj /Fe:{configuration}.exe\n'
        f'if errorlevel 1 exit /b 1\n{configuration}.exe\n', encoding="utf-8")
    print(configuration, flush=True)
    subprocess.run(["cmd.exe", "/d", "/c", str(driver)], cwd=out, check=True)
