#pragma once
#include "weapon_hud_warning.hpp"
#include "physical_reload_profile.hpp"
#include "tube_profile.hpp"

namespace vr::gameplay::weapons::chambering_guide
{
	inline bool supported(const tube_profile& p) noexcept
	{return p.ammunition.drive==tube::action_drive::automatic && !tube::rotary(p.ammunition) && !p.bolt_bone.empty();}
	inline bool needed(const tube_profile& p,const tube::state& ammo,bool action_held=false) noexcept
	{return supported(p) && weapon_hud::needs_chamber(p.ammunition,ammo,action_held);}
	inline bool needed(const reload_profile& p,const mechanics::state& ammo,bool action_held=false) noexcept
	{
		// A belt must be seated and its covers closed before suggesting a rack.
		return weapon_hud::needs_chamber(p.ammunition,ammo,action_held) &&
			(!p.ammunition.belt_fed || belt_feed::ready(ammo.belt));
	}
	inline bool catch_needed(const reload_profile& p,const mechanics::state& ammo) noexcept
	{
		return needed(p,ammo) && p.interaction.receiver_release &&
			ammo.action==action_state::locked_open && ammo.magazine_inserted && ammo.magazine_rounds>0;
	}
}
