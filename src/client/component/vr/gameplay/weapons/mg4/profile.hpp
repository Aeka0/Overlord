#pragma once
#include "poses.hpp"
#include "reload_profile.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::mg4
{
	inline bool suppress_equip(std::string_view n) noexcept
	{
		return base_equip_action(n, "h2_wpn_lmg_mg4_");
	}
	inline const profile base{
	    .id = "mg4",
	    .receiver = "h2_viewmodel_mg4_base",
	    .variant = "support",
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
	// Live gulag arctic receiver shares the reviewed 37-bone hierarchy and bind pose.
	// Its own reload recipe keeps the native material on detached ammunition boxes.
	inline const profile arctic = []
	{
		auto p = base;
		p.receiver = arctic_physical.rigid_magazine_source;
		p.reload = &arctic_physical;
		return p;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& r,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		const auto* selected = receiver.name == base.receiver     ? &base
		                       : receiver.name == arctic.receiver ? &arctic
		                                                          : nullptr;
		if (!selected || receiver.count != 37)
			return {};
		const auto bound = bind_attachment_set(rifle_attachments::common, models, receiver, r, bones);
		if (!bound.valid)
			return {nullptr, "Belt weapon attachment topology rejected"};
		return {selected, "Open-bolt box and belt feed", {}, bound.muzzle};
	}
}
