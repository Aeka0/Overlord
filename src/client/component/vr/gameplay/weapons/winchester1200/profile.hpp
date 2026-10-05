#pragma once
#include "poses.hpp"
#include "reload_poses.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::winchester1200
{
	inline constexpr std::array<std::string_view, 1> names{"winchester1200"};
	inline sound_reference sound(tube::effect e) noexcept
	{
		switch (e)
		{
		case tube::effect::draw:
			return {"h2_wpn_w1200_lift_plr"};
		case tube::effect::load_tube:
		case tube::effect::load_port:
			return {"weap_winch1200_loop_plr"};
		case tube::effect::rack_open:
			return {"h2_wpn_w1200_open_plr"};
		case tube::effect::rack_close:
			return {"h2_wpn_w1200_close_plr"};
		default:
			return {};
		}
	}
	inline constexpr tube::tuning interaction{
	    .rack =
	        {
	            .waist_radius = physical_reload::defaults::waist_radius_m,
	            .slide_radius = part_grip_capture::radius_m,
	            .slide_stroke = 0.07659368f,
	            .locked_travel = 0.07059368f,
	            .full_stroke = 0.07359368f,
	            .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
	            .well_radius = physical_reload::defaults::well_radius_m,
	            .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
	            .insertion_cosine = physical_reload::defaults::insertion_cosine,
	            .max_contact_step = physical_reload::defaults::max_contact_step_m,
	            .slide_axis = physical_reload::defaults::rearward_axis,
	            .well_capture_below = physical_reload::defaults::well_capture_below_m,
	        },
	    .port_radius = .035f,
	    .tube_radius = .035f,
	    .alignment = .35f,
	    .loading =
	        {
	            .sideways = .015f,
	            .down = .04f,
	        },
	};
	inline const tube_profile feed{
	    .id = "winchester1200",
	    .receiver = "h2_viewmodel_winchester1200_base",
	    .native_variants = names,
	    .ammunition =
	        {
	            .capacity = 7,
	            .drive = tube::action_drive::pump,
	        },
	    .interaction = interaction,
	    .bolt_rest = bolt_rest,
	    .lifter_rest = lifter_rest,
	    .lifter_loaded = lifter_loaded,
	    .shell_in_wrist = shell_in_wrist,
	    .shell_center = shell_center,
	    .port_center = port_center,
	    .tube_center = tube_center,
	    .port_forward = port_forward,
	    .tube_forward = tube_forward,
	    .rack_low = rack_low,
	    .rack_high = rack_high,
	    .rack_grips = {&rack_pose, 1},
	    .shell_fingers = shell_fingers,
	    .sound_key = sound,
	    .bolt_bone = "j_slide",
	    .lifter_bone = "j_load",
	    .shell_bone = "tag_clip",
	    .receiver_bones = 16,
	    .pump_bone = "j_pump",
	    .pump_rest = pump_rest,
	    .bolt_open = bolt_open,
	    .port_shell = port_shell,
	};
	inline bool suppress_equip(std::string_view n) noexcept
	{
		return base_equip_action(n, "h2_wpn_sho_w1200_");
	}
	inline const profile base = []
	{
		profile p{
		    .id = "winchester1200",
		    .receiver = "h2_viewmodel_winchester1200_base",
		    .variant = "pump",
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
		p.tube = &feed;
		return p;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		if (receiver.count != 16)
			return {nullptr, "pump receiver topology rejected"};
		const auto b = bind_attachment_set(rifle_attachments::common, models, receiver, rig, bones);
		if (!b.valid)
			return {nullptr, "pump attachment topology rejected"};
		return {&base, "manual pump tube feed", {}, b.muzzle};
	}
}
