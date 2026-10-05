#pragma once
#include "poses.hpp"
#include "../../weapon_actions.hpp"
#include "../../weapon_profile_binding.hpp"
namespace vr::gameplay::weapons::ranger
{
	inline bool suppress_equip(std::string_view name) noexcept
	{
		return base_equip_action(name, "h2_wpn_sho_ranger_");
	}
	inline const profile base = []
	{
		profile p{
		    .id = "ranger",
		    .receiver = feed.receiver,
		    .variant = "break_action",
		    .aiming = aim_rule::two_hand,
		    .wrists = wrists,
		    .authored_rear = 1,
		    .acquire_meters = profile_defaults::acquire_meters,
		    .release_meters = profile_defaults::release_meters,
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
		p.break_open = &feed;
		return p;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name != base.receiver || receiver.count != int(feed.receiver_bones))
			return {};
		return bind_profile_attachments(base, models, receiver, rig, bones);
	}
}
