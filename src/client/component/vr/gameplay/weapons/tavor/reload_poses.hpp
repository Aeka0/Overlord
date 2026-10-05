#pragma once
#include "magazine_collision.hpp"
#include "handle_poses.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::tavor
{
// h2_wpn_asl_tavor_reload frame 43: e04a591be621886350f5a11204ebb7b147ad715808df8c451e19079a8814c5bd
// h2_wpn_asl_tavor_pullout_first frame 13: abe5598bef2495dfd7f11b2b66f7db7c74c4958c604adb2c7a5525f98b7e54eb
// Handle contact retains the real receiver side; native le hand retargeted to left.
inline constexpr hands::anchor magazine_rest={{-7.67860070f, 0.00000000f, 2.34080032f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{8.85526702f, 0.70209498f, 4.14236024f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{1.64180310f, 1.89665619f, 7.64345809f}, {-0.05044509f, 0.03160927f, -0.08653912f, 0.99446826f}};
inline constexpr hands::anchor magazine_well={{-6.15287899f, -0.00214476f, -0.78740157f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={1.52572171f, -0.00214476f, 0.47179817f};
inline constexpr hands::vec action_grab_low={9.24646873f, 1.48713598f, 4.20267488f};
inline constexpr hands::vec action_grab_high={9.80216800f, 1.77952793f, 4.69398761f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.60133986f, -0.28673905f, 0.13412955f, 0.73360777f}},
	{"j_mid_le_0", {0.39524321f, -0.48274113f, 0.42208321f, 0.65771542f}},
	{"j_pinkypalm_le", {0.68994913f, -0.23196671f, -0.19036115f, 0.65872929f}},
	{"j_ringpalm_le", {0.71477704f, -0.11459749f, -0.10509655f, 0.68184743f}},
	{"j_thumb_le_0", {-0.25128488f, -0.09448428f, 0.16331713f, 0.94934511f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02343846f, -0.01366023f, 0.42796552f, 0.90338782f}},
	{"j_mid_le_1", {-0.01286446f, -0.01724297f, 0.22704349f, 0.97364698f}},
	{"j_pinky_le_0", {-0.17035437f, 0.04753169f, 0.73605998f, 0.65340327f}},
	{"j_ring_le_0", {-0.20625938f, -0.02154796f, 0.73333076f, 0.64747104f}},
	{"j_thumb_le_1", {0.10774334f, 0.08099689f, -0.19823136f, 0.97084252f}},
	{"j_index_le_2", {0.00253303f, 0.03630478f, 0.34515783f, 0.93783880f}},
	{"j_mid_le_2", {-0.02781680f, -0.00943204f, 0.14191148f, 0.98944348f}},
	{"j_pinky_le_1", {0.01756427f, 0.02174350f, 0.22601964f, 0.97372164f}},
	{"j_ring_le_1", {0.00793475f, 0.01146051f, 0.29654607f, 0.95491682f}},
	{"j_thumb_le_2", {-0.03884831f, -0.06065470f, 0.11743535f, 0.99046492f}},
	{"j_pinky_le_2", {-0.03360309f, -0.00192089f, 0.07727382f, 0.99644162f}},
	{"j_ring_le_2", {-0.00010771f, -0.00317394f, 0.09982653f, 0.99499979f}},
}};
inline const auto& action_fingers=handle_pose_fingers_0;

inline constexpr magazine_latch_profile forward_latch{{-4.55079417f,0,.82216103f},{-1,0,0}};
inline constexpr magazine_contact_profile contacts{{4.43713816f, 1.15941152f, 1.23088570f},{-8.27343516f, -0.53969096f, -4.64310763f},{-3.68069396f, 0.53969096f, 2.87743736f},magazine_latch,strike_regions,&forward_latch};
inline constexpr float action_stroke_m=0.11374228f;
}
