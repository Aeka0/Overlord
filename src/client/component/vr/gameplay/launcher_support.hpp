#pragma once
#include "launcher_profile.hpp"

namespace vr::gameplay::weapons::launcher
{
	enum class support_change {none,grab,release};
	struct support_transition {support_change change{};hand actor{hand::none};};
	// Observe committed carry ownership, not raw input or render proximity.
	// Identity/control changes are lifecycle events, not support releases.
	inline support_transition support_changed(const hold& before,const hold& after)noexcept
	{
		if(!before.id() || before.id()!=after.id() || !before.can_fire() || !after.can_fire() ||
			before.rear!=after.rear || before.rear_revision!=after.rear_revision || before.support==after.support)return {};
		if(before.support==hand::none && valid_hand(after.support) && after.support!=after.rear)return {support_change::grab,after.support};
		if(after.support==hand::none && valid_hand(before.support) && before.support!=before.rear)return {support_change::release,before.support};
		return {};
	}
	inline sound_reference support_sound(const launcher_profile& p,support_change change)noexcept
	{return change==support_change::grab?p.support_grab_sound:change==support_change::release?p.support_release_sound:sound_reference{};}
}
