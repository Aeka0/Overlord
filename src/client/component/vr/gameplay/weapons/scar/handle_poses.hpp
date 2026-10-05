#pragma once
#include "../../part_grip_pose.hpp"
#include "../hand_poses/left_handle.hpp"
namespace vr::gameplay::weapons::scar
{
// Complete anatomical chains; native source travel removed before mesh fitting.
// h2_wpn_asl_scar_h_pullout_first, frame 17.
// SHA256 72ba6315e99a1eb95ae985fbe68114543357536bbaae035383b9f1b3a1912cb1
inline constexpr std::array<joint_pose, 18> handle_pose_fingers_0{{
	{"j_index_le_0", {0.67663288f, -0.20539169f, -0.05005087f, 0.70532060f}},
	{"j_mid_le_0", {0.52650511f, -0.40021956f, 0.09686571f, 0.74379682f}},
	{"j_pinkypalm_le", {0.68993485f, -0.23192516f, -0.19036125f, 0.65875882f}},
	{"j_ringpalm_le", {0.71476400f, -0.11458115f, -0.10510633f, 0.68186235f}},
	{"j_thumb_le_0", {0.08654283f, -0.29879898f, 0.19260986f, 0.93066156f}},
	{"j_webbing_le", {-0.67247921f, -0.03613412f, -0.16806640f, 0.71987474f}},
	{"j_index_le_1", {0.01889100f, -0.01947085f, 0.65639347f, 0.75393081f}},
	{"j_mid_le_1", {-0.02102763f, -0.00427267f, 0.81775802f, 0.57516205f}},
	{"j_pinky_le_0", {-0.09631594f, 0.21573429f, 0.71641851f, 0.65644991f}},
	{"j_ring_le_0", {-0.09466752f, 0.12011971f, 0.66905093f, 0.72731024f}},
	{"j_thumb_le_1", {0.08469421f, 0.10489118f, -0.42977002f, 0.89281714f}},
	{"j_index_le_2", {0.01934868f, 0.03076257f, 0.74596184f, 0.66499644f}},
	{"j_mid_le_2", {-0.02777140f, 0.00946059f, 0.71384710f, 0.69968677f}},
	{"j_pinky_le_1", {0.02697828f, 0.00753805f, 0.74797583f, 0.66313463f}},
	{"j_ring_le_1", {0.01229876f, 0.00646982f, 0.70078540f, 0.71323675f}},
	{"j_thumb_le_2", {0.00567643f, -0.07180988f, -0.50475210f, 0.86025369f}},
	{"j_pinky_le_2", {-0.03048824f, 0.01425226f, 0.54097557f, 0.84036464f}},
	{"j_ring_le_2", {-0.00146490f, -0.00274669f, 0.52458692f, 0.85135120f}},
}};

inline const part_grip_pose native_grip{"native",{{5.45681906f, 4.10871172f, 3.54379749f}, {0.94266152f, -0.25359085f, 0.20871945f, -0.05930557f}},{5.06096663f, 0.14827779f, 0.49856238f},handle_pose_fingers_0};
inline const auto action_grips=[] {
	auto out=hand_poses::left_handle::with_native(native_grip);
	out[0].palm=part_palm_facing::down;out[1].palm=part_palm_facing::up;return out;
}();
}
