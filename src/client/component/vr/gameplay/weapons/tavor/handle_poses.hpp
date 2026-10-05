#pragma once
#include "../../part_grip_pose.hpp"
#include "../hand_poses/left_handle.hpp"
namespace vr::gameplay::weapons::tavor
{
// Complete anatomical chains; native source travel removed before mesh fitting.
// h2_wpn_asl_tavor_pullout_first, frame 11.
// SHA256 abe5598bef2495dfd7f11b2b66f7db7c74c4958c604adb2c7a5525f98b7e54eb
inline constexpr std::array<joint_pose, 18> handle_pose_fingers_0{{
	{"j_index_le_0", {0.58921591f, -0.33985126f, 0.12820718f, 0.72172616f}},
	{"j_mid_le_0", {0.46229306f, -0.44617934f, 0.25162439f, 0.72380542f}},
	{"j_pinkypalm_le", {0.68990180f, -0.23195393f, -0.19035733f, 0.65878446f}},
	{"j_ringpalm_le", {0.71475605f, -0.11459382f, -0.10508863f, 0.68187127f}},
	{"j_thumb_le_0", {0.27796425f, -0.41639704f, 0.23395629f, 0.83343496f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.04898172f, -0.06179936f, 0.49695843f, 0.86418398f}},
	{"j_mid_le_1", {-0.01858587f, -0.01062050f, 0.59636546f, 0.80242757f}},
	{"j_pinky_le_0", {-0.13516741f, 0.04944033f, 0.82745407f, 0.54277545f}},
	{"j_ring_le_0", {-0.19064953f, -0.04940957f, 0.70220185f, 0.68419589f}},
	{"j_thumb_le_1", {0.11120880f, 0.07605168f, -0.15475846f, 0.97872292f}},
	{"j_index_le_2", {0.02093567f, 0.01086457f, 0.44251779f, 0.89644948f}},
	{"j_mid_le_2", {-0.02929748f, 0.00170902f, 0.50300111f, 0.86378737f}},
	{"j_pinky_le_1", {0.02658197f, 0.00878945f, 0.71688924f, 0.69662469f}},
	{"j_ring_le_1", {0.01159702f, 0.00775169f, 0.62407212f, 0.78124222f}},
	{"j_thumb_le_2", {-0.04284764f, -0.05786263f, 0.18371995f, 0.98033819f}},
	{"j_pinky_le_2", {-0.03317346f, 0.00573745f, 0.30091101f, 0.95305780f}},
	{"j_ring_le_2", {-0.00073245f, -0.00308240f, 0.30118974f, 0.95355897f}},
}};

inline const part_grip_pose native_grip{"native",{{5.66108102f, 4.46635601f, 2.36134111f}, {0.88581471f, -0.29694321f, 0.33781247f, -0.11419176f}},{5.28608084f, -0.28055128f, 1.04163337f},handle_pose_fingers_0};
inline const auto action_grips=hand_poses::left_handle::with_native(native_grip);
}
