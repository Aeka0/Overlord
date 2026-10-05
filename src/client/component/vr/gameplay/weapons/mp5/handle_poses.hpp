#pragma once
#include "../../part_grip_pose.hpp"
#include "../hand_poses/left_handle.hpp"
namespace vr::gameplay::weapons::mp5
{
// Complete anatomical chains; native source travel removed before mesh fitting.
// h2_wpn_smg_mp5k_reload_empty, frame 11.
// SHA256 3c4c3e1bd72f0932df4fb98c2f90b8f2e461615c9465ab35df456202aa37d06c
inline constexpr std::array<joint_pose, 18> handle_pose_fingers_0{{
	{"j_index_le_0", {0.70573950f, -0.14535944f, 0.07892075f, 0.68889326f}},
	{"j_mid_le_0", {0.54698420f, -0.41834846f, 0.31327304f, 0.65395176f}},
	{"j_pinkypalm_le", {0.66232288f, -0.26940349f, -0.11719061f, 0.68921441f}},
	{"j_ringpalm_le", {0.68304187f, -0.13867976f, -0.10882809f, 0.70878643f}},
	{"j_thumb_le_0", {-0.19855533f, -0.10925732f, 0.24084569f, 0.94373298f}},
	{"j_webbing_le", {-0.67247921f, -0.03613412f, -0.16806640f, 0.71987474f}},
	{"j_index_le_1", {0.01732939f, -0.02088479f, 0.71263117f, 0.70101380f}},
	{"j_mid_le_1", {-0.01962356f, -0.00858933f, 0.67705506f, 0.73562056f}},
	{"j_pinky_le_0", {-0.09857445f, 0.18225591f, 0.43040106f, 0.87853330f}},
	{"j_ring_le_0", {-0.11621408f, 0.07370194f, 0.53782582f, 0.83174854f}},
	{"j_thumb_le_1", {0.06005168f, 0.06257163f, -0.39308694f, 0.91540223f}},
	{"j_index_le_2", {0.01843824f, 0.03136789f, 0.72654796f, 0.68615168f}},
	{"j_mid_le_2", {-0.02826009f, 0.00823235f, 0.68330055f, 0.72954363f}},
	{"j_pinky_le_1", {0.02652059f, 0.00912504f, 0.70736682f, 0.70629001f}},
	{"j_ring_le_1", {0.01214633f, 0.00684375f, 0.68407792f, 0.72927570f}},
	{"j_thumb_le_2", {0.03881092f, 0.00533203f, -0.00681872f, 0.99920911f}},
	{"j_pinky_le_2", {-0.02320911f, 0.02436880f, 0.80920607f, 0.58656037f}},
	{"j_ring_le_2", {0.00460823f, 0.03753725f, 0.79994011f, 0.59888697f}},
}};

inline const part_grip_pose native_grip{"native",{{6.80796623f, 4.25382090f, 4.84903765f}, {0.91049206f, -0.24039409f, 0.31115648f, -0.12804896f}},{4.91204311f, 0.88538808f, 1.04766739f},handle_pose_fingers_0};
inline const auto action_grips=hand_poses::left_handle::with_native(native_grip);
}
