#pragma once
#include "../../part_grip_pose.hpp"
#include "../hand_poses/left_handle.hpp"
namespace vr::gameplay::weapons::ump
{
// Complete anatomical chains; native source travel removed before mesh fitting.
// h2_wpn_smg_ump45_reload_empty, frame 66.
// SHA256 f8c075cb8ca1330049c42cf4643bcbdb91e6a15286b139681eb6daa0b654fcb1
inline constexpr std::array<joint_pose, 18> handle_pose_fingers_0{{
	{"j_index_le_0", {0.66510773f, -0.13202056f, 0.07791227f, 0.73084331f}},
	{"j_mid_le_0", {0.65141797f, -0.32401520f, 0.26508391f, 0.63277113f}},
	{"j_pinkypalm_le", {0.68432474f, -0.22327361f, -0.15659061f, 0.67626023f}},
	{"j_ringpalm_le", {0.71134788f, -0.10540324f, -0.09349339f, 0.68857342f}},
	{"j_thumb_le_0", {-0.11938777f, -0.22788049f, 0.26203054f, 0.93013817f}},
	{"j_webbing_le", {-0.67247921f, -0.03613412f, -0.16806640f, 0.71987474f}},
	{"j_index_le_1", {0.04055925f, -0.04434356f, 0.65654713f, 0.75188726f}},
	{"j_mid_le_1", {-0.05407898f, 0.00302134f, 0.69890678f, 0.71315896f}},
	{"j_pinky_le_0", {0.03402814f, 0.15796383f, 0.37757507f, 0.91177112f}},
	{"j_ring_le_0", {0.01486248f, 0.06521790f, 0.41416568f, 0.90774036f}},
	{"j_thumb_le_1", {0.07275621f, 0.15464357f, -0.36537412f, 0.91503751f}},
	{"j_index_le_2", {0.02002027f, 0.03036611f, 0.76110601f, 0.64760697f}},
	{"j_mid_le_2", {-0.02871809f, 0.00619530f, 0.62880725f, 0.77700603f}},
	{"j_pinky_le_1", {-0.00790425f, 0.01403844f, 0.77101541f, 0.63661265f}},
	{"j_ring_le_1", {-0.02221745f, 0.01156650f, 0.78426373f, 0.61992174f}},
	{"j_thumb_le_2", {-0.06400577f, -0.04315881f, -0.09759007f, 0.99222815f}},
	{"j_pinky_le_2", {-0.02551395f, 0.02197374f, 0.74829739f, 0.66250825f}},
	{"j_ring_le_2", {-0.03558479f, 0.00997961f, 0.66924423f, 0.74212283f}},
}};

inline const part_grip_pose native_grip{"native",{{9.41108608f, 4.71178484f, 4.24120617f}, {0.94819427f, -0.25702411f, 0.18177800f, 0.04269632f}},{4.94525229f, 1.29925713f, 0.60799204f},handle_pose_fingers_0};
inline const auto action_grips=hand_poses::left_handle::with_native(native_grip);
}
