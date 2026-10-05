#pragma once
#include "magazine_collision.hpp"
#include "handle_poses.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::ump
{
// h2_wpn_smg_ump45_reload frame 16: 45d74b893b9d04f4c830f460539bf2f94fc512860ef74e0a84824fc03334fa8a
// h2_wpn_smg_ump45_reload_empty frame 66: f8c075cb8ca1330049c42cf4643bcbdb91e6a15286b139681eb6daa0b654fcb1
// Native left grasp moved with the action pivot to its closed rest.
// Closest reference hand/tab skin: 0.450 mm. HMD fit pending.
inline constexpr hands::anchor magazine_rest={{6.58935562f, 0.00000000f, 2.94747991f}, {0.00000000f, -0.10367276f, 0.00000000f, 0.99461146f}};
inline constexpr hands::anchor action_rest={{12.82952526f, 0.00000000f, 5.16238475f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{6.02425561f, 5.01101532f, 0.91047604f}, {-0.71294441f, 0.22746779f, -0.16102946f, 0.64345799f}};
inline constexpr hands::anchor magazine_well={{6.12886031f, -0.00100554f, 1.67460014f}, {0.00000000f, -0.10367276f, 0.00000000f, 0.99461146f}};
inline constexpr hands::vec magazine_top={-0.71310021f, -0.00100554f, 0.92750757f};
inline constexpr hands::vec action_grab_low={12.87119107f, 1.14763900f, 5.07194264f};
inline constexpr hands::vec action_grab_high={13.49885895f, 1.60041400f, 5.47267809f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.68697077f, -0.23010927f, 0.21951936f, 0.65340045f}},
	{"j_mid_le_0", {0.59109073f, -0.31465022f, 0.18341880f, 0.71969753f}},
	{"j_pinkypalm_le", {0.68012158f, -0.19288897f, -0.21729384f, 0.67306156f}},
	{"j_ringpalm_le", {0.70796242f, -0.07404745f, -0.13346649f, 0.68955992f}},
	{"j_thumb_le_0", {0.00946063f, -0.14520536f, 0.51078229f, 0.84730594f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02142417f, -0.01663272f, 0.54497342f, 0.83801451f}},
	{"j_mid_le_1", {-0.01184141f, -0.01774175f, 0.64401361f, 0.76471660f}},
	{"j_pinky_le_0", {-0.09234819f, 0.04235931f, 0.49082481f, 0.86531411f}},
	{"j_ring_le_0", {-0.08243076f, -0.08877863f, 0.54002676f, 0.83288332f}},
	{"j_thumb_le_1", {0.00628678f, 0.07895098f, -0.31266053f, 0.94655724f}},
	{"j_index_le_2", {0.00991876f, 0.03493439f, 0.52992637f, 0.84726575f}},
	{"j_mid_le_2", {-0.02926735f, 0.00231941f, 0.52034231f, 0.85345294f}},
	{"j_pinky_le_1", {0.02362135f, 0.01508379f, 0.52323648f, 0.85172654f}},
	{"j_ring_le_1", {0.01054908f, 0.00909438f, 0.52364923f, 0.85182011f}},
	{"j_thumb_le_2", {-0.06982736f, -0.03903374f, 0.11615513f, 0.99000429f}},
	{"j_pinky_le_2", {-0.03218550f, 0.00988034f, 0.41865949f, 0.90751898f}},
	{"j_ring_le_2", {-0.00144457f, -0.00277725f, 0.52085065f, 0.85364208f}},
}};
inline const auto& action_fingers=handle_pose_fingers_0;

// Magazine body pull contact and receiver mouth are authored against mesh.
inline constexpr magazine_contact_profile contacts{{3.66425726f, 1.16989131f, 1.45759239f},{5.64484296f, -0.63937001f, -7.56350390f},{9.88240128f, 0.63736199f, 3.70995980f},magazine_latch,strike_regions};
// Native first_pullout frame 0 raised handle orientation.
inline constexpr charging_handle_catch handle_catch{{0.32908038f, 0.00000000f, 0.00000000f, 0.94430191f},{0.08941410f, 1.14924504f, 0.31029333f},{0.00000000f, 0.00000000f, 0.00000000f}};
}
