#pragma once
#include "actions.hpp"
#include "reload_profile.hpp"
#include "../../weapon_attachments.hpp"

namespace vr::gameplay::weapons::tmp
{
	inline constexpr profile base{
	    .id = "tmp",
	    .receiver = "h2_viewmodel_mp9_base",
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
	inline constexpr std::array<assembly_attachment, 1> attachments{
	    {{{"attach_h2_red_dot_sight_vm", "tag_red_dot", "tag_red_dot"}, attachment_role::optic, 2}}};
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name != base.receiver || receiver.count != 14)
			return {};
		const auto bound = bind_attachment_set(attachments, models, receiver, rig, bones);
		if (!bound.valid)
			return {nullptr, "TMP attachment topology rejected"};
		return {&base, "TMP receiver and reviewed reflex optic", {}, bound.muzzle};
	}
}
