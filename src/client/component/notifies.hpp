#pragma once

#include "game/scripting/lua/error.hpp"

namespace notifies
{
	extern bool hook_enabled;

	void set_lua_hook(const char* pos, const sol::protected_function&, bool is_variable = false);
	using gsc_hook_guard=bool(*)();
	// A redirect executes the target opcode directly; hooks on the target are
	// not chained. Put an observer on a subsequently dispatched instruction.
	void set_gsc_hook(const char* source, const char* target,gsc_hook_guard guard=nullptr);
	void clear_hook(const char* pos);
	size_t get_hook_count();

	void add_entity_damage_callback(const sol::protected_function&);
	// Registered during startup, retained across script/Lua reloads. Filters run
	// after Lua policy and before the original native damage calculation.
	using entity_damage_filter = int(*)(const game::gentity_s* target,
		const game::gentity_s* attacker, int damage, unsigned int means_of_death);
	void add_entity_damage_filter(entity_damage_filter);
	void clear_callbacks();
}
