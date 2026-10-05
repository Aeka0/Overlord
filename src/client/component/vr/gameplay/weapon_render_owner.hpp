#pragma once
#include "weapon_holding.hpp"

namespace vr::gameplay::weapons
{
	// Render-only lifetime check for an already matched native bone epoch.
	// A helper hand can enter/leave between scene registration and skinning.
	// Keep the consumed pose and its real owner; never use this for fire/input
	// authorization, and never substitute a latest or previous skeleton.
	inline bool same_render_carrier(const hold& a,const hold& b) noexcept
	{
		if(!a.weapon || a.id()!=b.id() || a.rear!=b.rear || a.rear_revision!=b.rear_revision ||
			a.attachment!=b.attachment || a.pose_rear!=b.pose_rear ||
			!valid_hand(a.holding_hand()) || !valid_hand(b.holding_hand()))return false;
		// Without a rear grip, support IS the carrier rather than an auxiliary
		// relationship. Its identity and lease revision must then stay exact.
		return valid_hand(a.rear) || (a.support==b.support && a.revision==b.revision);
	}
}
