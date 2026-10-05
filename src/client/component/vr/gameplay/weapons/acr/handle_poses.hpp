#pragma once
#include "../../part_grip_pose.hpp"
#include "../hand_poses/edge_handle.hpp"
namespace vr::gameplay::weapons::acr
{
// Complete anatomical chains; native source travel removed before mesh fitting.
// h2_wpn_asl_masada_pullout_first, frame 16.
// SHA256 1726d031bafa1f5a42c0b042685d8d0c4e2947391d50feb3fe579555b45abed6
inline constexpr std::array<joint_pose, 18> handle_pose_fingers_0{{
	{"j_index_le_0", {0.59177808f, -0.15652774f, -0.00131227f, 0.79075663f}},
	{"j_mid_le_0", {0.54477511f, -0.28616635f, 0.14776768f, 0.77426973f}},
	{"j_pinkypalm_le", {0.68286188f, -0.24472417f, -0.07530280f, 0.68420700f}},
	{"j_ringpalm_le", {0.70479197f, -0.15458981f, -0.03723246f, 0.69136388f}},
	{"j_thumb_le_0", {-0.01385526f, -0.38639206f, 0.18057673f, 0.90437893f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987477f}},
	{"j_index_le_1", {0.02294992f, -0.02221750f, 0.51316914f, 0.85769291f}},
	{"j_mid_le_1", {-0.01974525f, -0.00823990f, 0.69102212f, 0.72251689f}},
	{"j_pinky_le_0", {-0.12195275f, 0.10177985f, 0.61644725f, 0.77120761f}},
	{"j_ring_le_0", {-0.16479748f, -0.01406885f, 0.61786853f, 0.76868872f}},
	{"j_thumb_le_1", {0.08328539f, 0.10599142f, -0.44154419f, 0.88705586f}},
	{"j_index_le_2", {0.01501522f, 0.03308242f, 0.65035668f, 0.75875970f}},
	{"j_mid_le_2", {-0.02896210f, 0.00527971f, 0.60359534f, 0.79674713f}},
	{"j_pinky_le_1", {0.02432349f, 0.01385555f, 0.56450635f, 0.82495392f}},
	{"j_ring_le_1", {0.01205501f, 0.00698883f, 0.67135790f, 0.74100230f}},
	{"j_thumb_le_2", {-0.00573749f, -0.07174900f, -0.36344498f, 0.92883093f}},
	{"j_pinky_le_2", {-0.03085443f, 0.01342824f, 0.51726194f, 0.85516535f}},
	{"j_ring_le_2", {-0.00140387f, -0.00280773f, 0.50267265f, 0.86447114f}},
}};

inline const part_grip_fit right_index_fit{{{8.53624665f,0.61218408f,4.60251851f},{0.95555970f,-0.20212431f,0.17040106f,-0.13044119f}},
	{5.16482489f,0.29516130f,0.73699952f},handle_pose_fingers_0};
// Restore the accepted palm-up wrist; align both hooks in its anatomical bend plane.
inline constexpr auto underhand_fingers=[] {auto out=handle_pose_fingers_0;
	out[1].rotation={0.55427067f, -0.20018894f, -0.04537435f, 0.80662853f};
	out[7].rotation={-0.01743067f, -0.01456587f, 0.68106297f, 0.73187242f};
	out[12].rotation={-0.02719864f, 0.01108180f, 0.75539663f, 0.65460932f};
	return out;}();
inline constexpr hands::anchor underhand_wrist={{7.81414835f, -3.87756595f, 6.23783826f}, {0.13044123f, 0.17040109f, 0.20212431f, 0.95555969f}};
inline constexpr hands::vec underhand_contact={5.45238383f, -0.08011909f, 0.82394343f};
inline const std::array<part_grip_pose,2> action_grips=[] {
	auto hook=hand_poses::edge_handle::at({13.15748f,-1.74016f,5.248f})[1];
	// Restore the actual left underhand transform from reload_poses.hpp before
	// 37f582e (parent 6bf732b), then align its hook contact. The penetrating
	// left overhand transform is removed, not kept behind an acquisition gate.
	part_grip_pose underhand{"index_underhand",underhand_wrist,underhand_contact,underhand_fingers,nullptr,right_index_fit};
	underhand.palm=part_palm_facing::up;underhand.palm_hands=1;
	hook.palm=part_palm_facing::down;hook.palm_hands=1;
	return std::array<part_grip_pose,2>{underhand,hook};
}();
}
