#pragma once
#include "../../part_grip_pose.hpp"
#include "../hand_poses/left_handle.hpp"
namespace vr::gameplay::weapons::aug
{
// Complete anatomical chains; native source travel removed before mesh fitting.
// h2_wpn_asl_aug_grip_reload_empty, frame 92.
// SHA256 dbc49761de232c4e5c6e6142fb075b19db8b8fd3f4cb99abc1667241a898ba21
inline constexpr std::array<joint_pose, 18> handle_pose_fingers_0{{
	{"j_index_le_0", {0.76294744f, 0.00476098f, -0.11942131f, 0.63531649f}},
	{"j_mid_le_0", {0.76366222f, -0.00036622f, -0.13098502f, 0.63218892f}},
	{"j_pinkypalm_le", {0.69307965f, -0.20104066f, -0.15495092f, 0.67469508f}},
	{"j_ringpalm_le", {0.71484619f, -0.11403432f, -0.10561740f, 0.68178886f}},
	{"j_thumb_le_0", {0.22455276f, -0.43290040f, 0.19272231f, 0.85148776f}},
	{"j_webbing_le", {-0.67245191f, -0.03610378f, -0.16803668f, 0.71990871f}},
	{"j_index_le_1", {0.02420149f, -0.01223808f, 0.37352085f, 0.92722529f}},
	{"j_mid_le_1", {-0.01696826f, -0.01318397f, 0.47764429f, 0.87829047f}},
	{"j_pinky_le_0", {-0.07394674f, 0.02056959f, 0.75729036f, 0.64855230f}},
	{"j_ring_le_0", {-0.19391200f, -0.03756778f, 0.63062596f, 0.75053161f}},
	{"j_thumb_le_1", {0.11374267f, 0.07229847f, -0.12149438f, 0.98339951f}},
	{"j_index_le_2", {0.00042726f, 0.03637786f, 0.28943592f, 0.95650578f}},
	{"j_mid_le_2", {-0.02887075f, -0.00543234f, 0.27793437f, 0.96015078f}},
	{"j_pinky_le_1", {0.02401824f, 0.01443536f, 0.54549176f, 0.83764756f}},
	{"j_ring_le_1", {0.00970501f, 0.01004072f, 0.44542345f, 0.89521110f}},
	{"j_thumb_le_2", {-0.04354979f, -0.05737464f, 0.19611141f, 0.97793245f}},
	{"j_pinky_le_2", {-0.03344800f, -0.00387582f, 0.01864665f, 0.99925900f}},
	{"j_ring_le_2", {0.00015259f, -0.00317390f, 0.01922653f, 0.99981010f}},
}};

inline const part_grip_pose native_grip{"native",{{2.26777077f, 4.61238480f, 3.09516382f}, {0.93973970f, -0.22666413f, 0.19398676f, -0.16697857f}},{5.43308519f, 0.23241908f, 0.04876752f},handle_pose_fingers_0};
inline const auto action_grips=hand_poses::left_handle::with_native(native_grip);
}
