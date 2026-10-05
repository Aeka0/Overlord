#pragma once
#include "magazine_collision.hpp"
#include "../../physical_reload_profile.hpp"
#include "../hand_poses/edge_handle.hpp"
namespace vr::gameplay::weapons::dragunov
{
// h2_wpn_sni_dragunov_reload frame 20 SHA-256 c43a889202e14d744f145888fd3afdf677dd7581bcf023af752d6c0047642dca
inline constexpr hands::anchor magazine_rest={{7.55899084f, 0.01501200f, 2.14738602f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{9.92229999f, -0.38240027f, 2.55990046f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{5.31688609f, 3.24404291f, 0.41235705f}, {-0.74244059f, 0.16503717f, -0.09769523f, 0.64187253f}};
inline constexpr hands::anchor magazine_well={{6.38559228f, 0.00000000f, 0.00000000f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={-1.17339856f, -0.01501200f, -0.25595589f};
inline constexpr hands::vec action_grab_low={9.94104532f, -1.61597026f, 2.22833137f};
inline constexpr hands::vec action_grab_high={10.11804727f, -1.28467328f, 2.47874034f};
// Native right loading hand retargeted around the rigid magazine local plane.
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.64033465f, -0.10351800f, -0.03900236f, 0.76008840f}},
	{"j_mid_le_0", {0.61259389f, -0.07956117f, -0.00875875f, 0.78633455f}},
	{"j_pinkypalm_le", {0.70213764f, -0.18250022f, -0.13876729f, 0.67412169f}},
	{"j_ringpalm_le", {0.71682688f, -0.10013239f, -0.08990856f, 0.68414119f}},
	{"j_thumb_le_0", {-0.09256304f, -0.26056833f, 0.23719105f, 0.93127688f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987477f}},
	{"j_index_le_1", {0.02249210f, -0.01522873f, 0.48738004f, 0.87276738f}},
	{"j_mid_le_1", {-0.01953156f, -0.00891127f, 0.66486583f, 0.74665422f}},
	{"j_pinky_le_0", {-0.23304217f, 0.19318451f, 0.08462884f, 0.94932031f}},
	{"j_ring_le_0", {-0.21231798f, 0.07638811f, 0.06012170f, 0.97235349f}},
	{"j_thumb_le_1", {0.09799609f, 0.09262489f, -0.30650123f, 0.94227087f}},
	{"j_index_le_2", {0.00714127f, 0.03570647f, 0.46088817f, 0.88671086f}},
	{"j_mid_le_2", {-0.02914498f, 0.00421153f, 0.57475081f, 0.81779847f}},
	{"j_pinky_le_1", {0.02539114f, 0.01162744f, 0.63606026f, 0.77113387f}},
	{"j_ring_le_1", {0.01107814f, 0.00851459f, 0.56709120f, 0.82353649f}},
	{"j_thumb_le_2", {-0.04382482f, -0.05719208f, 0.19953116f, 0.97723885f}},
	{"j_pinky_le_2", {-0.03296038f, 0.00656155f, 0.32493450f, 0.94513921f}},
	{"j_ring_le_2", {-0.00201423f, -0.00238044f, 0.69385883f, 0.72010430f}},
}};
inline const auto action_grips=hand_poses::edge_handle::at({9.99026768f, -1.61597026f, 2.35353529f});
inline constexpr magazine_contact_profile contacts{{5.15492912f, -0.72634414f, 0.65630939f},{5.46496909f, -0.65619500f, -2.53418994f},{9.17743097f, 0.65619500f, 1.94668695f},{5.29445126f, 0.00000000f, 0.18788225f},strike_regions};
inline constexpr float action_stroke_m=0.15231047f;
}
