#pragma once
#include "../../part_grip_pose.hpp"
#include "../hand_poses/left_handle.hpp"
namespace vr::gameplay::weapons::fal
{
// Complete anatomical chains; native source travel removed before mesh fitting.
// h2_wpn_asl_fn_fal_first_pullout, frame 15.
// SHA256 7806379a3309db23793720dbd85480b2bd8195588d7c15a9b63ce99cbeacb430
inline constexpr std::array<joint_pose, 18> handle_pose_fingers_0{{
	{"j_index_le_0", {0.65309679f, -0.29456496f, -0.02105779f, 0.69731814f}},
	{"j_mid_le_0", {0.56867749f, -0.42130473f, 0.11285657f, 0.69740349f}},
	{"j_pinkypalm_le", {0.62073904f, -0.29282159f, -0.15716845f, 0.71009618f}},
	{"j_ringpalm_le", {0.69869667f, -0.14441481f, -0.09921652f, 0.69363058f}},
	{"j_thumb_le_0", {0.15512532f, -0.12552242f, 0.18027253f, 0.96316254f}},
	{"j_webbing_le", {-0.67247921f, -0.03613412f, -0.16806640f, 0.71987474f}},
	{"j_index_le_1", {0.04806636f, -0.03079299f, 0.58619595f, 0.80815572f}},
	{"j_mid_le_1", {-0.02084390f, -0.00531016f, 0.78869611f, 0.61440688f}},
	{"j_pinky_le_0", {-0.11233915f, 0.07278698f, 0.65493017f, 0.74373949f}},
	{"j_ring_le_0", {-0.14972347f, -0.02592160f, 0.67713171f, 0.72000247f}},
	{"j_thumb_le_1", {0.09646872f, 0.09415695f, -0.32149628f, 0.93726647f}},
	{"j_index_le_2", {0.01831103f, 0.03137290f, 0.72447598f, 0.68834221f}},
	{"j_mid_le_2", {-0.02934351f, -0.00134281f, 0.41181630f, 0.91079336f}},
	{"j_pinky_le_1", {0.02698961f, 0.00739687f, 0.75131059f, 0.65935516f}},
	{"j_ring_le_1", {0.01185253f, 0.00734346f, 0.65291917f, 0.75729924f}},
	{"j_thumb_le_2", {-0.02160717f, -0.06869738f, -0.14636724f, 0.98660552f}},
	{"j_pinky_le_2", {-0.02746636f, 0.01950111f, 0.68275261f, 0.72987270f}},
	{"j_ring_le_2", {-0.00204476f, -0.00241099f, 0.68404836f, 0.72942978f}},
}};

inline const part_grip_pose native_grip{"native",{{6.77720737f, 5.14539814f, 2.23942161f}, {0.90493721f, -0.37546420f, 0.20015305f, 0.00735196f}},{4.44040114f, -0.28720966f, 1.10430850f},handle_pose_fingers_0};
inline const auto action_grips=hand_poses::left_handle::with_native(native_grip);
}
