#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::pp2000
{
	inline bool suppress_equip(std::string_view animation) noexcept
	{
		return base_equip_action(animation, "h2_wpn_pst_pp2000_");
	}
	inline const profile base{
	    .id = "pp2000",
	    .receiver = "h2_viewmodel_p2000_base",
	    .variant = "foregrip",
	    .aiming = aim_rule::two_hand,
	    .wrists = wrists,
	    .authored_rear = 1,
	    .acquire_meters = profile_defaults::acquire_meters,
	    .release_meters = profile_defaults::release_meters,
	    .blend_seconds = profile_defaults::blend_seconds,
	    .fingers = idle_fingers,
	    .equip_rest = equip_rest,
	    .suppress_equip = suppress_equip,
	    .reload = &physical,
	    .viewmodel =
	        {
	            .visibility = part_visibility::rigid_groups,
	        },
	};
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name != base.receiver || receiver.count != 18)
			return {nullptr, "PP2000 receiver contract rejected"};
		const auto bound = bind_attachment_set(rifle_attachments::common, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "PP2000 attachment topology or cardinality rejected"};
		return {&base, "PP2000 integral foregrip / primary feed matched", {}, bound.muzzle};
	}
}
