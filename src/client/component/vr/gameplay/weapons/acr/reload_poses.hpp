#pragma once
#include "handle_poses.hpp"
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::acr
{
// h2_wpn_asl_masada_reload frame 24: 8362ec7c882486724dbed4ec396f442cc881864fab4b0cbd7e0383969d3b9941
// h2_wpn_asl_masada_pullout_first frame 16: 1726d031bafa1f5a42c0b042685d8d0c4e2947391d50feb3fe579555b45abed6
// Native RIGHT hand converted to LEFT overhand about the handle height,
// retaining its actual negative-Y side (same anatomical parity as AK).
// Handle fits and complete finger chains are authored in handle_poses.hpp.
inline constexpr hands::anchor magazine_rest={{6.60333934f, 0.00000000f, 2.98536387f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{12.80359133f, 0.00000000f, 5.56242184f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{8.12536591f, -1.22118589f, 8.82813418f}, {0.06747294f, 0.33802793f, -0.19700959f, 0.91780812f}};
inline constexpr hands::anchor magazine_well={{6.60333934f, 0.00000000f, -0.39370079f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
// Well is at the receiver mouth; top is the centreline magazine lip.
inline constexpr hands::vec magazine_top={0.00000000f, 0.00000000f, -1.44094488f};
inline constexpr hands::vec action_grab_low={12.51968504f, -1.81102362f, 5.11811024f}, action_grab_high={13.30708661f, -0.00787402f, 5.74803150f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.75489990f, -0.06434907f, -0.15520379f, 0.63395356f}},
	{"j_mid_le_0", {0.57749387f, -0.39505189f, 0.14548359f, 0.69947791f}},
	{"j_pinkypalm_le", {0.63976595f, -0.34713491f, -0.07429164f, 0.68167268f}},
	{"j_ringpalm_le", {0.70101749f, -0.18060345f, -0.04118622f, 0.68866579f}},
	{"j_thumb_le_0", {-0.20654781f, -0.09088348f, 0.21727499f, 0.94966824f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02035549f, -0.01794457f, 0.59490216f, 0.80333994f}},
	{"j_mid_le_1", {-0.01957763f, -0.00877407f, 0.67171249f, 0.74050122f}},
	{"j_pinky_le_0", {0.08549788f, 0.18961210f, 0.51916727f, 0.82897690f}},
	{"j_ring_le_0", {-0.09140254f, -0.04196277f, 0.42904263f, 0.89766760f}},
	{"j_thumb_le_1", {0.09996402f, 0.09042692f, -0.28568560f, 0.94879603f}},
	{"j_index_le_2", {0.00146488f, 0.03634746f, 0.31665928f, 0.94784156f}},
	{"j_mid_le_2", {-0.02932832f, 0.00184637f, 0.50660776f, 0.86167571f}},
	{"j_pinky_le_1", {0.02800042f, 0.00167850f, 0.87147697f, 0.48963358f}},
	{"j_ring_le_1", {0.01304664f, 0.00489821f, 0.78675056f, 0.61711372f}},
	{"j_thumb_le_2", {-0.01280258f, -0.07087960f, -0.26912885f, 0.96040712f}},
	{"j_pinky_le_2", {-0.03123549f, 0.01263458f, 0.49464070f, 0.86844418f}},
	{"j_ring_le_2", {-0.00109866f, -0.00296029f, 0.40913010f, 0.91247060f}},
}};
inline const auto& action_fingers=handle_pose_fingers_0;

}
