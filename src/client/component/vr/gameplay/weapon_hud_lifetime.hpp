#pragma once
#include "weapon_holding.hpp"

namespace vr::gameplay::weapon_hud
{
	inline bool has_presentation_owner(const weapons::hold& owner) noexcept
	{
		return owner.weapon && (vr::valid_hand(owner.rear) || vr::valid_hand(owner.support));
	}
	inline bool same_presentation_owner(const weapons::hold& a, const weapons::hold& b) noexcept
	{
		// Control revisions survive support-only changes but advance on stow,
		// redraw, pickup and handover, even when the native weapon token is reused.
		return has_presentation_owner(a) && has_presentation_owner(b) &&
			a.id() == b.id() && a.rear_revision == b.rear_revision &&
			a.rear == b.rear && a.source == b.source;
	}
	// Visibility preference and temporary tracking availability are independent
	// of this lease. Only the authoritative holding owner may retain its pixels
	// and anchor; a late render submission cannot reacquire a released lease.
	template<class Presentation> struct presentation_cache
	{
		Presentation value{};
		weapons::hold owner{};
		bool synchronize(const weapons::hold& current)
		{
			const bool changed = !same_presentation_owner(owner, current);
			if (changed) value = {};
			owner = current;
			return changed;
		}
		bool accepts(const weapons::hold& candidate) const noexcept
		{
			return same_presentation_owner(owner, candidate);
		}
	};
}
