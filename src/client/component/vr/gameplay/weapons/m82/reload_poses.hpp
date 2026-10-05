#pragma once
#include "../hand_poses/edge_handle.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::m82
{
	// h2_wpn_sni_m82_reload frame 80: b7310462cbbbc59719d2d2b0068c73565dcb4fe93f769fc4863823590fcd219e
	inline constexpr hands::anchor magazine_rest = {{5.40905975f, 0.01501200f, 3.61924584f},
													{0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor action_rest = {{8.89491284f, -1.09101402f, 4.41733195f},
												  {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor magazine_in_wrist = {{1.01358973f, 1.25708795f, 7.00997049f},
														{0.31791771f, -0.10426867f, -0.41846115f, -0.84436168f}};
	inline constexpr hands::anchor magazine_well = {{6.02820919f, -0.00004358f, 0.39370079f},
													{0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::vec magazine_top = {0.61914944f, -0.01505558f, -0.17440125f};
	inline constexpr hands::vec action_grab_low = {10.30071514f, -2.33198902f, 3.42796506f};
	inline constexpr hands::vec action_grab_high = {10.48608765f, -1.38833795f, 4.51988310f};
	inline constexpr std::array<joint_pose, 18> magazine_fingers{{
		{"j_index_le_0", {0.65127041f, -0.20453675f, 0.17548289f, 0.70937813f}},
		{"j_mid_le_0", {0.54197937f, -0.37290647f, 0.29783077f, 0.69173402f}},
		{"j_pinkypalm_le", {0.68985086f, -0.23209238f, -0.19016396f, 0.65884488f}},
		{"j_ringpalm_le", {0.71470032f, -0.11495902f, -0.10472575f, 0.68192404f}},
		{"j_thumb_le_0", {-0.20407931f, -0.26499486f, 0.32066118f, 0.88617480f}},
		{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
		{"j_index_le_1", {0.00518817f, -0.04528969f, 0.59203152f, 0.80462451f}},
		{"j_mid_le_1", {-0.01913478f, -0.01483175f, 0.44724120f, 0.89408567f}},
		{"j_pinky_le_0", {-0.10025221f, 0.10940766f, 0.41269731f, 0.89869928f}},
		{"j_ring_le_0", {-0.10904347f, 0.05853495f, 0.46614331f, 0.87601004f}},
		{"j_thumb_le_1", {0.15640809f, 0.27811647f, -0.14310196f, 0.93686155f}},
		{"j_index_le_2", {0.00125127f, 0.03634773f, 0.31129034f, 0.94961866f}},
		{"j_mid_le_2", {-0.02856546f, -0.00692773f, 0.22855422f, 0.97308735f}},
		{"j_pinky_le_1", {0.00674463f, 0.02780253f, 0.35032411f, 0.93619151f}},
		{"j_ring_le_1", {0.00988793f, 0.00988793f, 0.46158915f, 0.88698360f}},
		{"j_thumb_le_2", {-0.06567549f, -0.06781178f, -0.17480912f, 0.98006636f}},
		{"j_pinky_le_2", {-0.03436403f, 0.02380457f, 0.31412879f, 0.94845957f}},
		{"j_ring_le_2", {-0.00036622f, -0.00311284f, 0.17425800f, 0.98469504f}},
	}};
	// Both hands use the current index/pinky hooks on the exposed right tab.
	// No native-right override: it retained incomplete legacy finger chains.
	inline const auto action_grips=hand_poses::edge_handle::at({26.39923668f/2.54f,-5.71108801f/2.54f,8.91794257f/2.54f});
	inline constexpr magazine_contact_profile contacts{{4.51560504f, 0.27645428f, 1.27484403f},
													   {4.15725783f, -0.84647701f, -2.66691493f},
													   {10.68723483f, 0.84647701f, 3.50533808f},
													   {4.03937595f, 0.00000180f, 0.30630733f},{}};
	inline constexpr float action_stroke_m = 0.17183052f;
} // namespace vr::gameplay::weapons::m82
