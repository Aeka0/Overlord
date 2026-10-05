#pragma once
#include "physical_reload_runtime.hpp"
#include "cylinder_runtime.hpp"
#include "tube_runtime.hpp"
#include "break_action_runtime.hpp"
#include "launcher_runtime.hpp"
#include "weapon_carry_runtime.hpp"
#include "independent_fire_runtime.hpp"
#include "underbarrel_runtime.hpp"
#include "official_cheats.hpp"

namespace vr::gameplay::weapons::manual_feed
{
	// One set of native detours, distinct feed authorities. Calls are sequential
	// copied snapshots; no family lock is held while entering another family.
	inline void set_boundary_ready(unsigned bit) noexcept
	{ physical_reload::set_boundary_ready(bit); cylinder::set_boundary_ready(bit); tube::set_boundary_ready(bit); break_action::set_boundary_ready(bit); if(bit==1)launcher::set_boundary_ready(bit); }
	inline bool blocks_reload(const void* ps,int side) noexcept
	{ return underbarrel::blocks_native(ps) || physical_reload::blocks_reload(ps,side) || cylinder::blocks_reload(ps,side) || tube::blocks_reload(ps,side) || break_action::blocks_reload(ps,side) || launcher::blocks_reload(ps,side); }
	inline bool allow_fire(const void* ps,int command,int side) noexcept
	{
		if(cheats::transitioning() && native_ammunition::local_role(ps)>=0)return false;
		if(underbarrel::blocks_native(ps))return false;
		if (independent_fire::owns_native(ps)) return false; // Both simulation and prediction, including mouse attack.
		if (carry::active() && native_ammunition::local_role(ps)>=0)
		{
			const auto owner=carry::current_hold();
			if (side || !owner.can_fire() || native_ammunition::observe(ps).weapon!=owner.weapon) return false;
		}
		return physical_reload::allow_fire(ps,command,side) && cylinder::allow_fire(ps,command,side) && tube::allow_fire(ps,command,side) && break_action::allow_fire(ps,command,side) && launcher::allow_fire(ps,command,side);
	}
	inline void consumed(const void* ps,int command,std::uint32_t weapon,bool alternate,int amount,int side,
		const native_ammunition::snapshot& before,const native_ammunition::snapshot& after,bool sustained=false) noexcept
	{
		physical_reload::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
		cylinder::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
		tube::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
		break_action::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
		launcher::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
	}
}
