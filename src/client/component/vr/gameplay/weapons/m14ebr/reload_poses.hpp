#pragma once
#include "magazine_collision.hpp"
#include "../hand_poses/edge_handle.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::m14ebr
{
	// h2_wpn_sni_m14ebr_reload frame 47: f645b741d147375e27c46025da91cbe428bbfdbdc4c420811ea437cd9c5185e6
	// Charging grips share the AK edge-hand styles, fitted to the M14's real right tab.
	inline constexpr hands::anchor magazine_rest = {{8.71958169f, 0.00000000f, 2.65676900f},
													{0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor action_rest = {{9.10106569f, -0.67666398f, 5.00209388f},
												  {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor magazine_in_wrist = {{3.35799039f, 0.86199005f, 6.33173673f},
														{-0.07021045f, 0.06024752f, 0.15403550f, 0.98372445f}};
	inline constexpr hands::anchor magazine_well = {{8.07605027f, 0.00110793f, 0.59055118f},
													{0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::vec magazine_top = {-0.64353143f, 0.00110793f, 1.41327286f};
	inline constexpr hands::vec action_grab_low = {8.04752140f, -2.15353891f, 4.20701110f};
	inline constexpr hands::vec action_grab_high = {8.40004148f, -1.38094097f, 5.01488475f};
	inline constexpr std::array<joint_pose, 18> magazine_fingers{{
		{"j_index_le_0", {0.74029451f, -0.05533058f, 0.30061017f, 0.59877884f}},
		{"j_mid_le_0", {0.77052468f, -0.06527853f, 0.19775823f, 0.60243018f}},
		{"j_pinkypalm_le", {0.68436049f, -0.19440672f, -0.18119195f, 0.67898912f}},
		{"j_ringpalm_le", {0.71476144f, -0.11459499f, -0.10510390f, 0.68186307f}},
		{"j_thumb_le_0", {0.18347780f, -0.31272405f, 0.29935691f, 0.88256728f}},
		{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
		{"j_index_le_1", {0.02798542f, -0.00610369f, 0.53007497f, 0.84746686f}},
		{"j_mid_le_1", {-0.01477103f, -0.01464895f, 0.77895798f, 0.62673096f}},
		{"j_pinky_le_0", {-0.09100661f, 0.09176958f, 0.35844154f, 0.92456249f}},
		{"j_ring_le_0", {-0.03210538f, 0.07876805f, 0.25809432f, 0.96236800f}},
		{"j_thumb_le_1", {0.04177940f, 0.09219546f, -0.34772427f, 0.93211711f}},
		{"j_index_le_2", {0.00292980f, 0.02746688f, 0.07696829f, 0.99665083f}},
		{"j_mid_le_2", {-0.01889071f, -0.01361108f, 0.60218343f, 0.79801817f}},
		{"j_pinky_le_1", {0.02706991f, 0.00732444f, 0.75179275f, 0.65880288f}},
		{"j_ring_le_1", {0.01928732f, -0.01229872f, 0.66519901f, 0.74631563f}},
		{"j_thumb_le_2", {-0.02050820f, -0.05081272f, 0.04214556f, 0.99760776f}},
		{"j_pinky_le_2", {-0.02682578f, 0.02047793f, 0.70506935f, 0.70833484f}},
		{"j_ring_le_2", {-0.00219732f, -0.00228887f, 0.75444336f, 0.65635748f}},
	}};
	inline const auto action_grips=hand_poses::edge_handle::at({8.21649747f, -2.15291095f, 4.49429422f});
	inline constexpr magazine_contact_profile contacts{{4.81685092f, 1.09046953f, 1.05942714f},
													   {6.65775509f, -0.63084298f, -2.87248600f},
													   {10.93259796f, 0.63348596f, 4.13680602f},
													   {6.65775509f, 0.00000000f, 0.59055118f},strike_regions};
	inline constexpr float action_stroke_m = 0.09459206f;
} // namespace vr::gameplay::weapons::m14ebr
