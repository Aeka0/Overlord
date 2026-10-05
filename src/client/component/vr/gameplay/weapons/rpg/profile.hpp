#pragma once
#include "poses.hpp"
#include "../../weapon_actions.hpp"
#include "../../weapon_profile_binding.hpp"
namespace vr::gameplay::weapons::rpg
{
	inline bool suppress_equip(std::string_view name) noexcept
	{
		return base_equip_action(name, "h2_wpn_lau_rpg_") || base_equip_action(name, "h1_wpn_lau_rpg_");
	}
	inline constexpr std::array<attachment_contract, 1> rockets{
	    {{"h2_viewmodel_rpg7_rocket", "tag_clip", "tag_clip"}}};
	inline const profile base = []
	{
		profile out{
		    .id = "rpg",
		    .receiver = "h2_viewmodel_rpg7_base",
		    .variant = "launcher",
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
		            .hidden_attachments = {},
		            .visible_attachments = rockets,
		        },
		};
		out.launcher = &feed;
		out.free_hand_reference = &free_wrists;
		return out;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.name != base.receiver || receiver.count != 8)
			return {nullptr, "launcher receiver contract rejected"};
		unsigned rockets_found{};
		for (const auto& m : models)
			if (m.name == feed.rocket_model && (m.count != 2 || ++rockets_found > 1))
				return {nullptr, "RPG rocket attachment contract rejected"};
		return bind_profile_attachments(base, models, receiver, rig, bones);
	}
}
