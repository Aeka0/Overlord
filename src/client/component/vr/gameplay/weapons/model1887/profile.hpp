#pragma once
#include "poses.hpp"
#include "reload_poses.hpp"
#include "../attachments/rifle.hpp"
#include "../../weapon_actions.hpp"

namespace vr::gameplay::weapons::model1887
{
	inline constexpr std::array<std::string_view, 1> names{"model1887"};
	inline sound_reference sound(tube::effect e) noexcept
	{
		switch (e)
		{
		case tube::effect::draw:
			return {"weap_m1887_lift_plr"};
		case tube::effect::load_tube:
		case tube::effect::load_port:
			return {"weap_m1887_loop_plr"};
		case tube::effect::rack_open:
			return {"weap_m1887_open_plr"};
		case tube::effect::rack_close:
			return {"weap_m1887_close_plr"};
		default:
			return {};
		}
	}
	inline const tube_profile feed = []
	{
		tube_profile p{};
		p.id = "model1887";
		p.receiver = "h2_viewmodel_model_1887_base";
		p.native_variants = names;
		// Preserve the captured five-round native budget until a separate plus-one admission.
		p.ammunition = {
		    .capacity = 5,
		    .drive = tube::action_drive::lever,
		    .layout = tube::feed_layout::tube,
		    .chamber_bonus = false,
		};
		// The common feed's travel is metres; lever input itself uses radians.
		p.interaction.rack = {
		    .waist_radius = physical_reload::defaults::waist_radius_m,
		    .slide_radius = part_grip_capture::radius_m,
		    .slide_stroke = .10f,
		    .locked_travel = .095f,
		    .full_stroke = .097f,
		    .slide_lateral_limit = physical_reload::defaults::slide_lateral_limit_m,
		    .well_radius = physical_reload::defaults::well_radius_m,
		    .well_contact_depth = physical_reload::defaults::well_contact_depth_m,
		    .insertion_cosine = physical_reload::defaults::insertion_cosine,
		    .max_contact_step = physical_reload::defaults::max_contact_step_m,
		    .slide_axis = physical_reload::defaults::rearward_axis,
		    .well_capture_below = physical_reload::defaults::well_capture_below_m,
		    .close_travel = .002f,
		};
		// Broad shell contact and 120-degree approach tolerance for controller loading.
		p.interaction.port_radius = .075f;
		p.interaction.tube_radius = .075f;
		p.interaction.alignment = -.5f;
		p.interaction.loading = {
		    .sideways = .02f,
		    .down = .025f,
		};
		p.interaction.lever.spin_open = spin_open;
		p.bolt_rest = bolt_rest;
		p.bolt_open = bolt_rest;
		p.lifter_rest = p.lifter_loaded = action_rest;
		p.shell_in_wrist = shell_in_wrist;
		p.shell_center = shell_center;
		p.port_center = port_center;
		p.tube_center = tube_center;
		p.port_forward = port_forward;
		p.tube_forward = tube_forward;
		p.port_shell = port_shell;
		p.shell_fingers = shell_fingers;
		p.sound_key = sound;
		// Captured receiver tag_brass, native units. Offset the shell root so
		// feedback's centre composition recovers the exact native marker.
		hands::anchor brass{{6.52105301f, 0, 4.91204975f},
		                    {-.21261597f, -.21261597f, -.67434692f, .67434692f}};
		brass.rotation = hands::normalize(brass.rotation);
		brass.position = hands::sub(brass.position, hands::rotate(brass.rotation, shell_center));
		p.ejection_shell = brass;
		p.bolt_bone = "j_bolt";
		p.lifter_bone = "j_action";
		p.shell_bone = "j_ammo_01";
		p.receiver_bones = 9;
		p.lever = &lever_motion;
		return p;
	}();
	inline bool suppress_equip(std::string_view n) noexcept
	{
		return base_equip_action(n, "h2_wpn_sho_model1887_") ||
		       base_equip_action(n, "viewmodel_model1887_akimbo_");
	}
	inline const profile base = []
	{
		profile p{
		    .id = "model1887",
		    .receiver = feed.receiver,
		    .variant = "lever",
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
		constexpr std::array<std::string_view, 9> bones_expected{"j_gun",
		                                                         "j_action",
		                                                         "j_ammo_01",
		                                                         "j_ammo_02",
		                                                         "j_ammo_03",
		                                                         "j_bolt",
		                                                         "tag_brass",
		                                                         "tag_flash",
		                                                         "j_hammer"};
		if (receiver.count != 9 || receiver.begin < 0 || size_t(receiver.begin + 9) > bones.size())
			return {nullptr, "lever receiver topology rejected"};
		for (int i = 0; i < 9; ++i)
			if (bones[receiver.begin + i].name != bones_expected[i] ||
			    (i && rig.parent[receiver.begin + i] != receiver.begin + (i == 8 ? 1 : 0)))
				return {nullptr, "lever receiver hierarchy rejected"};
		const auto attachments = bind_attachment_set(rifle_attachments::common, models, receiver, rig, bones);
		return attachments.valid ? profile_match{&base, "manual lever tube feed", {}, attachments.muzzle}
		                         : profile_match{nullptr, "lever attachment topology rejected"};
	}
}
