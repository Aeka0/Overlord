#pragma once
#include <span>
#include <string_view>
namespace precision_test_data
{
	struct bone
	{
		std::string_view name;
		int parent;
	};
	struct model
	{
		std::string_view name;
		std::span<const bone> bones;
	};
	inline constexpr bone bones_0[]{{"j_gun", -1}, {"j_reload", 0}, {"tag_acog_2", 0}, {"tag_bipods", 0},
	    {"tag_brass", 0}, {"tag_clip", 0}, {"tag_flash", 0}, {"tag_foregrip", 0}, {"tag_heartbeat", 0},
	    {"tag_m14ebr_scope", 0}, {"tag_rail", 0}, {"tag_sight_off", 0}, {"tag_sight_on", 0}, {"tag_silencer", 0},
	    {"tag_silencer_off", 0}, {"tag_silencer_on", 0}, {"tag_thermal_scope", 0}, {"j_bullet", 5}};
	inline constexpr bone bones_1[]{{"j_gun", -1}, {"j_reload", 0}, {"tag_acog_2", 0}, {"tag_bipods", 0},
	    {"tag_brass", 0}, {"tag_clip", 0}, {"tag_flash", 0}, {"tag_foregrip", 0}, {"tag_heartbeat", 0},
	    {"tag_m14ebr_scope", 0}, {"tag_rail", 0}, {"tag_sight_off", 0}, {"tag_sight_on", 0}, {"tag_silencer", 0},
	    {"tag_silencer_off", 0}, {"tag_silencer_on", 0}, {"tag_thermal_scope", 0}, {"j_bullet", 5}};
	inline constexpr bone bones_2[]{{"j_gun", -1}, {"j_bolt", 0}, {"j_handle", 0}, {"j_magrelease", 0}, {"j_recoil", 0},
	    {"j_ring1", 0}, {"j_ring2", 0}, {"j_trigger", 0}, {"tag_acog_2", 0}, {"tag_bipods", 0}, {"tag_brass", 0},
	    {"tag_clip", 0}, {"tag_front_sight_on", 0}, {"tag_heartbeat", 0}, {"tag_m82_scope", 0}, {"tag_sight_off", 0},
	    {"tag_sight_on", 0}, {"tag_thermal_scope", 0}, {"tag_bullet2", 11}, {"tag_flash", 4}, {"tag_silencer", 4},
	    {"tag_bullet", 18}, {"tag_bullet_single", 21}};
	inline constexpr bone bones_3[]{{"j_gun", -1}, {"j_ammo", 0}, {"j_bolt", 0}, {"j_reload", 0}, {"j_trigger", 0},
	    {"tag_acog_2", 0}, {"tag_brass", 0}, {"tag_clip", 0}, {"tag_flash", 0}, {"tag_heartbeat", 0},
	    {"tag_silencer", 0}, {"tag_thermal_scope", 0}, {"tag_wa2000_scope", 0}};
	inline constexpr bone bones_4[]{{"j_gun", -1}, {"j_bolt", 0}, {"j_clip_release", 0}, {"j_le_bipod", 0},
	    {"j_ri_bipod", 0}, {"j_ring", 0}, {"j_trigger", 0}, {"tag_acog_2", 0}, {"tag_brass", 0},
	    {"tag_cheytac_scope", 0}, {"tag_clip", 0}, {"tag_flash", 0}, {"tag_heartbeat", 0}, {"tag_laser_box", 0},
	    {"tag_silencer", 0}, {"tag_thermal_scope", 0}, {"j_bullet", 10}};
	inline constexpr bone bones_5[]{
	    {"tag_cheytac_scope", -1}, {"tag_scope_ads_off", 0}, {"tag_scope_ads_on", 0}, {"tag_reticle_attach", 2}};
	inline constexpr bone bones_6[]{{"tag_silencer", -1}, {"tag_flash_silenced", 0}};
	inline constexpr bone bones_7[]{{"tag_acog_2", -1}, {"tag_reticle_acog", 0}};
	inline constexpr bone bones_8[]{{"tag_acog_2", -1}, {"tag_reticle_acog", 0}};
	inline constexpr bone bones_9[]{{"tag_acog_2", -1}, {"tag_reticle_acog", 0}};
	inline constexpr bone bones_10[]{{"tag_acog_2", -1}, {"tag_reticle_acog", 0}};
	inline constexpr bone bones_11[]{{"tag_eotech", -1}, {"tag_eotech_reticle", 0}};
	inline constexpr bone bones_12[]{{"tag_eotech", -1}, {"tag_eotech_reticle", 0}};
	inline constexpr bone bones_13[]{{"tag_heartbeat", -1}, {"j_motion_tracker_roty", 0}, {"j_motion_tracker_rotz", 1},
	    {"tag_motion_tracker", 2}, {"j_wire_base", 3}, {"tag_screen_bl", 3}, {"tag_screen_br", 3}, {"tag_screen_tl", 3},
	    {"tag_screen_tr", 3}};
	inline constexpr bone bones_14[]{{"tag_laser", -1}};
	inline constexpr bone bones_15[]{{"tag_bipods", -1}, {"j_pod_left", 0}, {"j_pod_left_spring", 0},
	    {"j_pod_right", 0}, {"j_pod_right_spring", 0}, {"j_pod_left_spring_end", 1}, {"j_pod_right_spring_end", 3}};
	inline constexpr bone bones_16[]{
	    {"tag_sight_on", -1}, {"tag_scope_ads_off", 0}, {"tag_scope_ads_on", 0}, {"tag_reticle_attach", 2}};
	inline constexpr bone bones_17[]{
	    {"tag_sight_on", -1}, {"tag_scope_ads_off", 0}, {"tag_scope_ads_on", 0}, {"tag_reticle_attach", 2}};
	inline constexpr bone bones_18[]{{"tag_bipods", -1}, {"j_pod_left", 0}, {"j_pod_right", 0}};
	inline constexpr bone bones_19[]{{"tag_sight_on", -1}, {"tag_scope_ads_off", 0}, {"tag_scope_ads_on", 0},
	    {"j_flap_back", 1}, {"j_flap_front", 1}, {"tag_reticle_attach", 2}};
	inline constexpr bone bones_20[]{{"tag_red_dot", -1}, {"tag_reticle_red_dot", 0}};
	inline constexpr bone bones_21[]{{"tag_red_dot", -1}, {"tag_reticle_red_dot", 0}};
	inline constexpr bone bones_22[]{{"tag_red_dot", -1}, {"tag_reticle_red_dot", 0}};
	inline constexpr bone bones_23[]{{"tag_red_dot", -1}, {"tag_reticle_red_dot", 0}};
	inline constexpr bone bones_24[]{{"tag_silencer", -1}, {"tag_flash_silenced", 0}};
	inline constexpr bone bones_25[]{{"tag_thermal_scope", -1}, {"tag_scope_ads_off", 0}, {"tag_scope_ads_on", 0},
	    {"tag_reticle_attach", 2}, {"tag_reticle_thermal_scope", 3}};
	inline constexpr bone bones_26[]{{"tag_thermal_scope", -1}, {"tag_scope_ads_off", 0}, {"tag_scope_ads_on", 0},
	    {"tag_reticle_attach", 2}, {"tag_reticle_thermal_scope", 3}};
	inline constexpr bone bones_27[]{{"tag_thermal_scope", -1}, {"tag_scope_ads_off", 0}, {"tag_scope_ads_on", 0},
	    {"tag_reticle_attach", 2}, {"tag_reticle_thermal_scope", 3}};
	inline constexpr bone bones_28[]{{"tag_wa2000_scope", -1}, {"tag_scope_ads_off", 0}, {"tag_scope_ads_on", 0},
	    {"j_front_cover", 1}, {"j_rear_cover", 1}, {"tag_reticle_attach", 2}};
	inline constexpr model models[]{{"h2_viewmodel_m14ebr_base", bones_0}, {"h2_viewmodel_m14ebr_base_arctic", bones_1},
	    {"h2_viewmodel_m82_base", bones_2}, {"h2_viewmodel_wa2000_base", bones_3},
	    {"h2_viewmodel_cheytac_base", bones_4}, {"attach_h2_cheytac_scope_vm", bones_5},
	    {"attach_h2_silencer_03_vm", bones_6}, {"attach_h2_acog_2_vm", bones_7},
	    {"attach_h2_acog_2_vm_arctic", bones_8}, {"attach_h2_acog_2_vm_digital", bones_9},
	    {"attach_h2_acog_2_vm_tan", bones_10}, {"attach_h2_eotech_2_vm", bones_11},
	    {"attach_h2_eotech_2_vm_digital", bones_12}, {"attach_h2_heartbeat_vm", bones_13},
	    {"attach_h2_laser_peq6_vm", bones_14}, {"attach_h2_m14ebr_bipod_vm", bones_15},
	    {"attach_h2_m14ebr_scope_vm", bones_16}, {"attach_h2_m14ebr_scope_vm_arctic", bones_17},
	    {"attach_h2_m82_bipod_vm", bones_18}, {"attach_h2_m82_scope_vm", bones_19},
	    {"attach_h2_red_dot_sight_vm", bones_20}, {"attach_h2_red_dot_sight_vm_arctic", bones_21},
	    {"attach_h2_red_dot_sight_vm_digital", bones_22}, {"attach_h2_red_dot_sight_vm_tan", bones_23},
	    {"attach_h2_silencer_01_vm", bones_24}, {"attach_h2_thermal_scope_2_vm", bones_25},
	    {"attach_h2_thermal_scope_2_vm_arctic", bones_26}, {"attach_h2_thermal_scope_2_vm_tan", bones_27},
	    {"attach_h2_wa2000_scope_vm", bones_28},
		// Live cheytac_silencer_desert: exact hierarchy and bind matrices match.
		{"h2_viewmodel_cheytac_base_desert",bones_4},{"attach_h2_cheytac_scope_vm_desert",bones_5},
		// Estate woodland optics have the exact base hierarchy and bind matrices.
		{"attach_h2_acog_2_vm_woodland",bones_7},{"attach_h2_eotech_2_vm_woodland",bones_11},
		{"attach_h2_red_dot_sight_vm_woodland",bones_20}};
} // namespace precision_test_data
