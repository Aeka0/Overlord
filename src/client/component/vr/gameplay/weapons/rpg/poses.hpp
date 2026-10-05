#pragma once
#include "../../launcher_profile.hpp"
namespace vr::gameplay::weapons::rpg
{
// h2_wpn_lau_rpg_pullout frame 21; SHA256 e7c65dc806c557f819cd8dbb451ccfe38e1d28216c35a9384d94b329ed6a1284
// Source left hand is on the forward firing grip; source right is on the rear support.
// Exchange roles through anatomical mirroring, not by assigning opposite-hand quaternions.
inline constexpr std::array<hands::anchor,2> free_wrists{{{{-4.68140098f, 1.32877846f, -1.82641547f}, {0.73602264f, -0.06215832f, 0.06834069f, 0.67062402f}},{{-9.90759108f, -1.45355727f, -2.03226589f}, {-0.06104839f, -0.63465499f, 0.76407471f, -0.09836657f}}}};
inline constexpr std::array<hands::anchor,2> wrists{{{{-9.90759108f, 1.45355727f, -2.03226589f}, {0.76407468f, -0.09836658f, 0.06104837f, 0.63465502f}},{{-4.68140098f, -1.32877846f, -1.82641547f}, {-0.06834071f, -0.67062399f, 0.73602266f, -0.06215830f}}}};
inline constexpr std::array<joint_pose, 36> idle_fingers{{
	{"j_index_le_0", {0.46772790f, -0.37592800f, 0.35874602f, 0.71498954f}},
	{"j_index_ri_0", {0.64909295f, -0.06646879f, 0.09332483f, 0.75203106f}},
	{"j_mid_le_0", {0.46336110f, -0.38166328f, 0.36753324f, 0.71031609f}},
	{"j_mid_ri_0", {0.63233558f, -0.30777534f, 0.20285400f, 0.68137824f}},
	{"j_pinkypalm_le", {0.62575635f, -0.20322429f, -0.12311258f, 0.74294829f}},
	{"j_pinkypalm_ri", {0.68993586f, -0.23197253f, -0.19034505f, 0.65874579f}},
	{"j_ringpalm_le", {0.71691504f, -0.09936894f, -0.08914517f, 0.68426003f}},
	{"j_ringpalm_ri", {0.71477862f, -0.11459773f, -0.10507589f, 0.68184892f}},
	{"j_thumb_le_0", {-0.05203367f, -0.40958619f, 0.25113515f, 0.87547860f}},
	{"j_thumb_ri_0", {0.02795499f, -0.37620271f, 0.13599084f, 0.91607671f}},
	{"j_webbing_le", {-0.67244430f, -0.03613389f, -0.16809582f, 0.71990055f}},
	{"j_webbing_ri", {-0.67246718f, -0.03616399f, -0.16803287f, 0.71989236f}},
	{"j_index_le_1", {0.01950159f, -0.01876918f, 0.62875169f, 0.77713494f}},
	{"j_index_ri_1", {0.02099706f, -0.01709061f, 0.56356306f, 0.82562929f}},
	{"j_mid_le_1", {-0.01980626f, -0.00827040f, 0.68903738f, 0.72440790f}},
	{"j_mid_ri_1", {-0.01971459f, -0.00839244f, 0.68549482f, 0.72776215f}},
	{"j_pinky_le_0", {-0.04101688f, 0.08371221f, 0.45774693f, 0.88418190f}},
	{"j_pinky_ri_0", {-0.08380288f, 0.19095218f, 0.06991714f, 0.97551316f}},
	{"j_ring_le_0", {-0.15906204f, 0.04385495f, 0.54582215f, 0.82149509f}},
	{"j_ring_ri_0", {-0.08960237f, 0.01040681f, 0.20450458f, 0.97470046f}},
	{"j_thumb_le_1", {0.12085199f, 0.01702922f, -0.47873900f, 0.86943302f}},
	{"j_thumb_ri_1", {0.09637652f, 0.09427064f, -0.32208899f, 0.93706099f}},
	{"j_index_le_2", {0.00582904f, 0.03588998f, 0.42793310f, 0.90307873f}},
	{"j_index_ri_2", {0.00521869f, 0.03598127f, 0.41395243f, 0.90957215f}},
	{"j_mid_le_2", {-0.02935859f, -0.00073243f, 0.42933107f, 0.90266958f}},
	{"j_mid_ri_2", {-0.02926725f, 0.00289925f, 0.53794990f, 0.84246360f}},
	{"j_pinky_le_1", {0.02652055f, 0.00906400f, 0.70964630f, 0.70400045f}},
	{"j_pinky_ri_1", {0.02096625f, 0.01779231f, 0.46055553f, 0.88720485f}},
	{"j_ring_le_1", {0.01159691f, 0.00781264f, 0.62147287f, 0.78331089f}},
	{"j_ring_ri_1", {0.01226838f, 0.00677509f, 0.68803506f, 0.72554210f}},
	{"j_thumb_le_2", {0.00564585f, -0.07180935f, -0.50471049f, 0.86027836f}},
	{"j_thumb_ri_2", {-0.01647978f, -0.05655011f, -0.37302305f, 0.92595049f}},
	{"j_pinky_le_2", {-0.02658144f, 0.02066091f, 0.71254114f, 0.70082214f}},
	{"j_pinky_ri_2", {-0.03164788f, 0.01141398f, 0.46119825f, 0.88665907f}},
	{"j_ring_le_2", {-0.00143438f, -0.00280772f, 0.50560189f, 0.86276114f}},
	{"j_ring_ri_2", {-0.00146486f, -0.00271612f, 0.52702221f, 0.84984591f}},
}};
inline constexpr std::array<part_pose,7> equip_rest{{
{"j_front_aim_flip",{{6.14518218f, 0.58259199f, 3.61503804f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}}},
{"j_rear_aim_flip",{{-4.34917014f, 0.60591299f, 3.94062883f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}}},
{"j_safety",{{-1.64618999f, 0.00000000f, 0.53811402f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}}},
{"j_trigger",{{-0.00692600f, 0.00000000f, 0.33240300f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}}},
{"tag_clip",{{14.17322835f, 0.00000000f, 2.36220491f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}}},
{"tag_flash",{{17.95782780f, 0.01628315f, 0.10825743f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}}},
{"tag_flash_silenced",{{6.86274476f, 0.00000000f, 2.36220491f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}}},
}};
// Rocket grasp: native reload frame 25; SHA256 d1d016ab8f12bb2bdefca429a36770e447d2b9d8f0d8615688f69ac38c63d3a1
inline constexpr std::array<joint_pose, 18> rocket_fingers{{
	{"j_index_le_0", {0.64252670f, -0.27466883f, 0.05240071f, 0.71342178f}},
	{"j_mid_le_0", {0.51228389f, -0.43835277f, 0.28449365f, 0.68152434f}},
	{"j_pinkypalm_le", {0.68993588f, -0.23197251f, -0.19034503f, 0.65874578f}},
	{"j_ringpalm_le", {0.71477861f, -0.11459774f, -0.10507590f, 0.68184892f}},
	{"j_thumb_le_0", {0.14421446f, -0.28014319f, 0.36406866f, 0.87645650f}},
	{"j_webbing_le", {-0.67246718f, -0.03616399f, -0.16803287f, 0.71989236f}},
	{"j_index_le_1", {0.01879954f, -0.01950147f, 0.65972340f, 0.75102018f}},
	{"j_mid_le_1", {-0.01849412f, -0.01078823f, 0.58734075f, 0.80905650f}},
	{"j_pinky_le_0", {-0.06428771f, 0.14356012f, 0.64862987f, 0.74467232f}},
	{"j_ring_le_0", {-0.13200850f, -0.01716675f, 0.59947820f, 0.78924328f}},
	{"j_thumb_le_1", {0.10791295f, 0.08084316f, -0.19681296f, 0.97112504f}},
	{"j_index_le_2", {-0.00134282f, 0.03631717f, 0.24280624f, 0.96939383f}},
	{"j_mid_le_2", {-0.02896219f, -0.00515765f, 0.28878267f, 0.95694261f}},
	{"j_pinky_le_1", {0.01643403f, 0.02261396f, 0.17631869f, 0.98393610f}},
	{"j_ring_le_1", {0.00976603f, 0.01005596f, 0.44932908f, 0.89325633f}},
	{"j_thumb_le_2", {-0.01881478f, -0.06953688f, -0.18637775f, 0.97983364f}},
	{"j_pinky_le_2", {-0.03277670f, 0.00753803f, 0.35267001f, 0.93514316f}},
	{"j_ring_le_2", {-0.00096134f, -0.00306712f, 0.35632026f, 0.93435836f}},
}};
inline constexpr std::array<std::string_view,3> aliases{"rpg_player","rpg_af_chase","rpg_straight_af_chase"};
// Separate quiet transients in the verified 48000 Hz / 40969-frame lift recording.
// Boundaries selected at low-energy millisecond positions; no extracted asset ships.
inline constexpr sound_window support_grab_window{
	.recording = "wpfoly_rpg_reload_lift_v1",
	.begin_ms = 108,
	.end_ms = 167,
};
inline constexpr sound_window support_release_window{
	.recording = "wpfoly_rpg_reload_lift_v1",
	.begin_ms = 443,
	.end_ms = 498,
};
// Last 6 cm of the exported rocket tail; muzzle ring from the receiver mesh.
inline const launcher_profile feed{
	.id = "rpg",
	.native_name = "rpg",
	.loading = launcher_loading::rocket,
	.guided = false,
	.aliases = aliases,
	.rocket_model = "h2_viewmodel_rpg7_rocket",
	.rocket_rest = {{14.17322835f, 0.00000000f, 2.36220491f},
	                {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}},
	.rocket_in_wrist = {{4.58213829f, 2.96699486f, 0.39517690f},
	                    {0.54185468f, 0.43461453f, 0.24802303f, 0.67526906f}},
	.rocket_fingers = rocket_fingers,
	.tail_start = {-43.3238678f / 2.54f, 0, 0},
	.tail_end = {-37.3238678f / 2.54f, 0, 0},
	.load_mouth = {18.09935f / 2.54f, 0, 6.f / 2.54f},
	.load_sound =
	    {
	        .name = "weap_rpg_insert_plr",
	    },
	.support_grab_sound =
	    {
	        .name = "weap_rpg_lift_plr",
	        .kind = sound_reference_kind::notetrack,
	        .part = sound_part::whole,
	        .notetrack_weapon = nullptr,
	        .window = &support_grab_window,
	    },
	.support_release_sound =
	    {
	        .name = "weap_rpg_lift_plr",
	        .kind = sound_reference_kind::notetrack,
	        .part = sound_part::whole,
	        .notetrack_weapon = nullptr,
	        .window = &support_release_window,
	    },
};
}
