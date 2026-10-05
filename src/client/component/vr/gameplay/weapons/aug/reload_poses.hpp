#pragma once
#include "magazine_collision.hpp"
#include "handle_poses.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::aug
{
// h2_wpn_asl_aug_grip_reload frame 28: 0e94a8a78b06f27abac475f1ce1e06a0cf0eea9da9233c8c7b7f9cd2a3306c7d
// h2_wpn_asl_aug_grip_reload_empty frame 92: dbc49761de232c4e5c6e6142fb075b19db8b8fd3f4cb99abc1667241a898ba21
// Native left grasp moved with the action pivot to its closed rest.
// Closest reference hand/tab skin: 0.334 mm. HMD fit pending.
inline constexpr hands::anchor magazine_rest={{-6.10595313f, 0.00000000f, 1.89595598f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{6.64069784f, 0.00000000f, 3.66276005f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{4.76317365f, 2.97040965f, 1.62139352f}, {-0.63239138f, 0.42902105f, -0.04168967f, 0.64364902f}};
inline constexpr hands::anchor magazine_well={{-6.15627828f, 0.02184242f, 1.18110236f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={-0.05032516f, 0.02184242f, 1.11473766f};
inline constexpr hands::vec action_grab_low={6.38326660f, 1.25879997f, 3.49251379f};
inline constexpr hands::vec action_grab_high={7.98240196f, 1.93649990f, 4.80351411f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.66991304f, -0.12464184f, 0.07491668f, 0.72805798f}},
	{"j_mid_le_0", {0.47507690f, -0.32452720f, 0.29077068f, 0.76448443f}},
	{"j_pinkypalm_le", {0.68993177f, -0.23191010f, -0.19034390f, 0.65877238f}},
	{"j_ringpalm_le", {0.71479603f, -0.11456512f, -0.10507398f, 0.68183643f}},
	{"j_thumb_le_0", {-0.19910147f, -0.35608427f, 0.30393526f, 0.86092157f}},
	{"j_webbing_le", {-0.67245194f, -0.03610378f, -0.16803669f, 0.71990873f}},
	{"j_index_le_1", {0.02286161f, -0.01449620f, 0.46165060f, 0.88664871f}},
	{"j_mid_le_1", {-0.01492351f, -0.01538129f, 0.34873404f, 0.93697667f}},
	{"j_pinky_le_0", {-0.07413086f, 0.05740640f, 0.33787678f, 0.93650862f}},
	{"j_ring_le_0", {-0.13543379f, 0.03960548f, 0.45755730f, 0.87791253f}},
	{"j_thumb_le_1", {0.10483194f, 0.08477279f, -0.23264450f, 0.96317204f}},
	{"j_index_le_2", {0.00559315f, 0.03592821f, 0.42220496f, 0.90577086f}},
	{"j_mid_le_2", {-0.02895411f, -0.00499123f, 0.29317290f, 0.95560787f}},
	{"j_pinky_le_1", {0.01964622f, 0.01995141f, 0.32041277f, 0.94686410f}},
	{"j_ring_le_1", {0.01023906f, 0.00943794f, 0.49659442f, 0.86787100f}},
	{"j_thumb_le_2", {-0.03408913f, -0.06347502f, 0.04044714f, 0.99658059f}},
	{"j_pinky_le_2", {-0.03164721f, 0.01162737f, 0.46704812f, 0.88358888f}},
	{"j_ring_le_2", {-0.00054933f, -0.00311284f, 0.24264930f, 0.97010892f}},
}};
inline const auto& action_fingers=handle_pose_fingers_0;

// Magazine body pull contact and receiver mouth are authored against mesh.
inline constexpr magazine_contact_profile contacts{{3.26452300f, 0.05654943f, 1.78355019f},{-7.57189097f, -0.76100596f, -5.14841080f},{-3.13914892f, 0.80469096f, 3.07997381f},magazine_latch,strike_regions};
// Authored +X 15-degree catch about the guide centre, not the receiver axis.
// Guide centre from j_reload vertices with Y < 2.7 cm; native clips only translate.
inline constexpr charging_handle_catch handle_catch{{0.13052619f, 0.00000000f, 0.00000000f, 0.99144486f},{0.03050331f, 1.76129998f, 0.86625392f},{0.00000000f, 0.93957547f, 0.05625346f}};
}
