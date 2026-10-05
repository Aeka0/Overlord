#pragma once
#include "physical_reload_runtime.hpp"
#include "weapon_render_owner.hpp"

namespace vr::gameplay::weapons::physical_reload
{
	enum class attachment_kind { held_magazine, inserted_magazine, chamber_round, action_partition };

	// Eligibility only. Placement still comes from the exact native skinned
	// object/matrices/epoch; this never permits a previous or latest pose fallback.
	inline bool attachment_state_matches(attachment_kind kind,const presentation& frozen,
		const presentation& live,clock::time_point captured_at,clock::time_point now) noexcept
	{
		if(!frozen.active || frozen.fault || !live.active || live.fault || !frozen.definition ||
			frozen.definition!=live.definition || !same_render_carrier(frozen.owner,live.owner) ||
			frozen.ammo.instance_generation!=live.ammo.instance_generation ||
			frozen.reference_generation!=live.reference_generation ||
			now<captured_at || now-captured_at>std::chrono::milliseconds(150))return false;

		// Support acquisition happens after this tick's reload update. A newly
		// skinned receiver can therefore precede the reload view's support owner.
		// Its rigid parts belong to the receiver, not that offhand lease. With no
		// rear hand, however, support is the actual carrier and must still match.
		if(kind==attachment_kind::held_magazine && frozen.owner.support!=live.owner.support)return false;

		const auto& p=*frozen.definition;
		switch(kind)
		{
		case attachment_kind::held_magazine:
			return valid_hand(frozen.ammo.magazine_hand) && frozen.ammo.magazine_hand==live.ammo.magazine_hand &&
				frozen.magazine_pose==live.magazine_pose && frozen.knife_magazine_grasp==live.knife_magazine_grasp &&
				p.magazine_subset(frozen.ammo.held_rounds)==p.magazine_subset(live.ammo.held_rounds);
		case attachment_kind::inserted_magazine:
			return frozen.ammo.magazine_inserted && live.ammo.magazine_inserted &&
				p.magazine_subset(frozen.ammo.magazine_rounds)==p.magazine_subset(live.ammo.magazine_rounds);
		case attachment_kind::chamber_round:
			return frozen.ammo.chamber_loaded && live.ammo.chamber_loaded &&
				!frozen.ammo.bolt.spent_case && !live.ammo.bolt.spent_case && !frozen.ammo.bolt.feeding && !live.ammo.bolt.feeding;
		case attachment_kind::action_partition:
			return true;
		}
		return false;
	}
}
