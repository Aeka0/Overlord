#pragma once
#include "component/vr/gameplay/hands/pose_math.hpp"

namespace vr::gameplay::weapons::hand_poses::shell_grasp
{
// SPAS-12 reload_loop frame 6, native right hand retargeted to canonical left.
// SHA256 b48a6dd6baaa2e3807b4311c4673b1d406a4344aae7d73659174c2154bf6c30a
inline constexpr hands::anchor shell_in_wrist={{4.65427956f, 1.15972092f, 1.42379840f}, {-0.57774301f, 0.07272148f, 0.04794621f, 0.81155762f}};
inline constexpr std::array<joint_pose, 18> shell_fingers{{
	{"j_index_le_0", {0.63707782f, -0.40660063f, 0.26667237f, 0.59807494f}},
	{"j_mid_le_0", {0.55762964f, -0.45563760f, 0.42078574f, 0.55170909f}},
	{"j_pinkypalm_le", {0.68994078f, -0.23194362f, -0.19034636f, 0.65875044f}},
	{"j_ringpalm_le", {0.71476622f, -0.11456525f, -0.10507409f, 0.68186764f}},
	{"j_thumb_le_0", {0.23826192f, -0.22587120f, 0.39580099f, 0.85764505f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987477f}},
	{"j_index_le_1", {0.02212619f, -0.01565621f, 0.50591192f, 0.86215918f}},
	{"j_mid_le_1", {-0.02124064f, -0.00283820f, 0.85380549f, 0.52015091f}},
	{"j_pinky_le_0", {-0.00540177f, 0.03360064f, 0.78371045f, 0.62019331f}},
	{"j_ring_le_0", {-0.04617420f, -0.00466936f, 0.78865449f, 0.61308257f}},
	{"j_thumb_le_1", {0.06738489f, 0.11676404f, -0.56294516f, 0.81542516f}},
	{"j_index_le_2", {0.01852460f, 0.03122030f, 0.72999971f, 0.68248264f}},
	{"j_mid_le_2", {-0.02885525f, 0.00555437f, 0.61212453f, 0.79021521f}},
	{"j_pinky_le_1", {0.02719217f, 0.00671413f, 0.76879718f, 0.63887903f}},
	{"j_ring_le_1", {0.01306213f, 0.00482196f, 0.79111434f, 0.61150979f}},
	{"j_thumb_le_2", {-0.03482164f, -0.06305132f, 0.05282756f, 0.99600263f}},
	{"j_pinky_le_2", {-0.02774139f, 0.01910464f, 0.67238525f, 0.73943458f}},
	{"j_ring_le_2", {-0.00192272f, -0.00247205f, 0.66021525f, 0.75106992f}},
}};
// The shell is rigid: adapt its model origin, never mirror or rescale its mesh.
inline hands::anchor for_shell(hands::vec model_in_source) noexcept
{
	return hands::pose_math::compose(shell_in_wrist,{model_in_source,{0,0,0,1}});
}
}
