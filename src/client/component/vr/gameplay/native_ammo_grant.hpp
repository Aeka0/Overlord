#pragma once
#include "detachable_magazine.hpp"

namespace vr::gameplay::weapons::mechanics
{
	struct grant_reconciliation
	{
		bool valid{};
		ammo_projection after{};
		state next{};
	};
	// Native pickups/scripts can credit the clip as well as reserve. Route their
	// budget into reserve without inventing a physical insertion/chamber/closure.
	// This is a bounded delta policy, not a claim about a particular script event.
	inline grant_reconciliation reconcile_native_grant(const rules& r, const state& current,
		ammo_projection observed, int reserve_limit) noexcept
	{
		if (!valid(r,current) || current.revision==UINT64_MAX) return {};
		const auto reserve=ammunition::granted_reserve(native_ammo(current),observed,r.magazine_capacity,
			r.magazine_capacity+int(r.plus_one),reserve_limit);
		if (!reserve) return {};
		auto next = current;
		next.reserve_rounds = *reserve;
		++next.revision;
		if (!valid(r,next)) return {};
		return {true,native_ammo(next),next};
	}
}
