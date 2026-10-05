#pragma once
#include "handle_poses.hpp"
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::tmp
{
// h2_wpn_pst_mp9_reload frame 30: 608e2933b8d413c09cf798f271fd05405b2460a840ec95e220b81e17308303c7
// h2_wpn_pst_mp9_pullout_first frame 17: 68ffbf548b9d8c941b0bf84f226beef83714ed29f22eca3bf13d3ae4a2a7644d
// Handle fits and complete finger chains are authored in handle_poses.hpp.
inline constexpr hands::anchor magazine_rest={{0.00244200f, 0.00000000f, 2.33000489f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{-3.45639094f, 0.00000000f, 3.97802188f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{6.72764562f, 8.49001779f, -0.28233332f}, {-0.76217770f, -0.03454339f, -0.30501913f, 0.56996074f}};
inline constexpr hands::anchor magazine_well={{-0.34180307f, -0.01770463f, -2.78810535f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
// Well is at the grip mouth, above the protruding magazine baseplate.
inline constexpr hands::vec magazine_top={-0.34424507f, -0.01770463f, 0.47836108f};
inline constexpr hands::vec action_grab_low={-3.81496063f, -0.78740157f, 3.59055118f}, action_grab_high={-2.87401575f, 0.77559055f, 4.32677165f};
inline constexpr std::array<joint_pose,15> magazine_fingers{{
	{"j_index_le_0", {0.45281053f, -0.32141692f, 0.38511261f, 0.73711740f}},
	{"j_mid_le_0", {0.45377966f, -0.39620432f, 0.33521527f, 0.72438725f}},
	{"j_thumb_le_0", {-0.13976680f, -0.15272569f, 0.44887275f, 0.86928325f}},
	{"j_index_le_1", {0.00173956f, -0.03361844f, 0.49177263f, 0.87007267f}},
	{"j_mid_le_1", {-0.01887778f, -0.01014517f, 0.61715522f, 0.78654952f}},
	{"j_pinky_le_0", {-0.11570886f, 0.08236953f, 0.68395096f, 0.71556817f}},
	{"j_ring_le_0", {-0.21597984f, 0.02932833f, 0.57241916f, 0.79046117f}},
	{"j_thumb_le_1", {0.22078185f, 0.13519400f, -0.23116260f, 0.93783890f}},
	{"j_index_le_2", {0.01197624f, 0.03429817f, 0.57897939f, 0.81453242f}},
	{"j_mid_le_2", {-0.02932807f, 0.00148232f, 0.49622731f, 0.86769587f}},
	{"j_pinky_le_1", {-0.03024392f, -0.00740294f, 0.68416562f, 0.72866172f}},
	{"j_ring_le_1", {0.01237749f, 0.00651791f, 0.70160637f, 0.71242741f}},
	{"j_thumb_le_2", {0.05896194f, -0.04165789f, 0.26463990f, 0.96164122f}},
	{"j_pinky_le_2", {-0.15422396f, 0.10188878f, 0.79291011f, 0.58062656f}},
	{"j_ring_le_2", {-0.00125125f, -0.00285565f, 0.45777603f, 0.88906208f}},
}};
inline const auto& action_fingers=handle_pose_fingers_0;

}
