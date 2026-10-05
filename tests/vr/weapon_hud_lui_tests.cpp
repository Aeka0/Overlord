#include <cstdio>
extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

int run_suite(const char* adapter, const char* suite)
{
	auto* state = luaL_newstate();
	if (!state) return 2;
	luaL_openlibs(state);
	lua_pushstring(state, adapter);
	lua_setglobal(state, "adapter_path");
	const auto result = luaL_dofile(state, suite);
	if (result) std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
	lua_close(state);
	return result ? 1 : 0;
}

int main(int argc, char** argv)
{
	const auto* adapter = argc > 1 ? argv[1] : "data/ui_scripts/vr_gameplay/__init__.lua";
	if (argc > 2) return run_suite(adapter, argv[2]);
	const auto adapter_result = run_suite(adapter, "tests/vr/weapon_hud_lui_tests.lua");
	const auto instance_result = run_suite(adapter, "tests/vr/weapon_hud_instance_tests.lua");
	return adapter_result ? adapter_result : instance_result;
}
