#pragma once
#include "underbarrel_feed.hpp"
#include <cmath>

namespace vr::gameplay::weapons::underbarrel
{
	// Choose only for a new waist draw. Existing escrow keeps its identity until
	// the player explicitly exchanges it through the compared reserve transfer.
	inline bool prefer_secondary_supply(const state& ammo,float travel,float stroke,bool primary_needed) noexcept
	{
		if(primary_needed || !valid(ammo) || ammo.held || !ammo.reserve)return false;
		switch(ammo.id.type)
		{
		case kind::m203:
			// The open latch survives partial closing. Check actual travel against
			// the same threshold that admits opening, rather than the latch alone.
			return !ammo.loaded && ammo.open && !ammo.spent && std::isfinite(travel) &&
				std::isfinite(stroke) && stroke>0 && stroke<=.5f && travel<=stroke &&
				travel>=stroke*action_open_fraction;
		case kind::gp25:return !ammo.loaded;
		case kind::shotgun:return ammo.loaded-int(ammo.chamber)<shotgun_tube_capacity;
		default:return false;
		}
	}
	inline bool select_secondary_supply(bool smart,bool preferred,bool grip_down) noexcept
	{return grip_down!=(smart && preferred);}
}
