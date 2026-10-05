#pragma once
#include "poses.hpp"
#include "reload_poses.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"
namespace vr::gameplay::weapons::spas12
{
	inline constexpr std::array<std::string_view, 3> names{"spas12", "spas12_reflex", "spas12_eotech"};
	inline constexpr std::array<std::string_view, 2> arctic_names{"spas12_arctic", "spas12_arctic_reflex"};
	inline sound_reference sound(tube::effect e) noexcept
	{
		switch (e)
		{
		case tube::effect::draw:
			return {"weap_spas12_lift_plr"};
		case tube::effect::load_tube:
		case tube::effect::load_port:
			return {"weap_spas12_loop_plr"};
		case tube::effect::rack_open:
			return {"weap_spas12_open_plr"};
		case tube::effect::rack_close:
			return {"weap_spas12_close_plr"};
		default:
			return {};
		}
	}
	inline constexpr tube::tuning interaction{
	    .rack =
	        {
	            .waist_radius = physical_reload::defaults::waist_radius_m,
	            .slide_radius = part_grip_capture::radius_m,
	            .slide_stroke = 0.07340460f,
	            .locked_travel = 0.06740460f,
	            .full_stroke = 0.07040460f,
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
	    .id = "spas12",
	    .receiver = "h2_viewmodel_spas12_base",
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
	    .bolt_bone = "j_reload",
	    .lifter_bone = "j_reload_plate",
	    .shell_bone = "tag_clip",
	    .receiver_bones = 17,
	    .pump_bone = "j_pump",
	    .pump_rest = pump_rest,
	    .bolt_open = bolt_open,
	    .port_shell = port_shell,
	};
	// Oilrig live assets: the arctic receiver's 17 bones, parents and all bind
	// transforms exactly match the base. Keep separate model/native admission.
	inline const tube_profile arctic_feed = []
	{
		auto p = feed;
		p.receiver = "h2_viewmodel_spas12_base_arctic";
		p.native_variants = arctic_names;
		return p;
	}();
	inline bool suppress_equip(std::string_view n) noexcept
	{
		return base_equip_action(n, "h2_wpn_sho_spas12_");
	}
	inline const profile base = []
	{
		profile p{
		    .id = "spas12",
		    .receiver = "h2_viewmodel_spas12_base",
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
	inline const profile arctic = []
	{
		auto p = base;
		p.receiver = arctic_feed.receiver;
		p.tube = &arctic_feed;
		return p;
	}();
	inline profile_match select(std::span<const hands::model_definition> models,
	                            const hands::model_definition& receiver,
	                            const hands::rig& rig,
	                            std::span<const hands::bone_definition> bones) noexcept
	{
		const auto* p = receiver.name == base.receiver     ? &base
		                : receiver.name == arctic.receiver ? &arctic
		                                                   : nullptr;
		if (!p || receiver.count != 17)
			return {nullptr, "pump receiver topology rejected"};
		const auto b = bind_attachment_set(rifle_attachments::common, models, receiver, rig, bones);
		if (!b.valid)
			return {nullptr, "pump attachment topology rejected"};
		return {p, "manual pump tube feed", {}, b.muzzle};
	}
}
