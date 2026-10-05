#pragma once
#include "poses.hpp"
#include "../../weapon_actions.hpp"
#include "../../weapon_profile_binding.hpp"
namespace vr::gameplay::weapons::javelin
{
	inline bool suppress_equip(std::string_view name) noexcept
	{
		// Override the solved parts only: native reload timers, notetracks/audio
		// and ammo-add events continue on their original animation timeline.
		return base_equip_action(name, "h2_wpn_lau_javelin_") || name == "h2_wpn_lau_javelin_reload" ||
		       name == "h2_wpn_lau_javelin_reload_empty";
	}
	inline const profile base = []
	{
		profile p{
		    .id = "javelin",
		    .receiver = "h2_viewmodel_javelin_base",
		    .variant = "launcher",
		    .aiming = aim_rule::two_hand,
		    .wrists = wrists,
		    .authored_rear = 1,
		    .acquire_meters = .14f,
		    .release_meters = .26f,
		    .blend_seconds = profile_defaults::blend_seconds,
		    .fingers = idle_fingers,
		    .equip_rest = equip_rest,
		    .suppress_equip = suppress_equip,
		    .reload = nullptr,
		    .viewmodel =
		        {
		            .visibility = part_visibility::rigid_groups,
		        },
		};
		p.launcher = &feed;
		p.control_grips = &wrists;
		p.support_grips = &wrists;
		return p;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name != base.receiver || receiver.count != 6)
			return {nullptr, "Javelin receiver contract rejected"};
		return bind_profile_attachments(base, models, receiver, rig, bones);
	}
}
