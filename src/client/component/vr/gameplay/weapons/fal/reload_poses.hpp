#pragma once
#include "handle_poses.hpp"
#include "magazine_collision.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::fal
{
// h2_wpn_asl_fn_fal_reload frame 22: 3e30e21b66a06ddcaaf80a843e0ebede4d63a5d572b15ab35c5eade0d544a9eb
// h2_wpn_asl_fn_fal_first_pullout frame 15: 7806379a3309db23793720dbd85480b2bd8195588d7c15a9b63ce99cbeacb430
// New-magazine grasp maps identical mesh to seated tag_clip_02.
inline constexpr hands::anchor magazine_rest={{6.18519835f, 0.00000000f, 2.53514981f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{9.02092626f, 0.00000000f, 3.36328491f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{4.89575167f, 5.29145951f, 0.67491327f}, {-0.70867653f, 0.22816744f, -0.19620444f, 0.63813871f}};
inline constexpr hands::anchor magazine_well={{7.19389207f, -0.00496798f, 1.14173228f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={1.00869373f, -0.00496798f, 0.94616084f};
inline constexpr hands::vec action_grab_low={9.46738776f, 1.79189697f, 2.72277096f};
inline constexpr hands::vec action_grab_high={10.28462658f, 2.19732495f, 3.54001090f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.82129573f, -0.06073273f, 0.11264548f, 0.55596389f}},
	{"j_mid_le_0", {0.58838282f, -0.28298610f, -0.06028383f, 0.75504330f}},
	{"j_pinkypalm_le", {0.68993588f, -0.23197251f, -0.19034503f, 0.65874578f}},
	{"j_ringpalm_le", {0.71477881f, -0.11456725f, -0.10510645f, 0.68184912f}},
	{"j_thumb_le_0", {-0.02526908f, -0.33875210f, 0.41410155f, 0.84446930f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {-0.18085362f, 0.05868740f, 0.43672212f, 0.87927331f}},
	{"j_mid_le_1", {0.02319440f, -0.03906425f, 0.69973842f, 0.71295312f}},
	{"j_pinky_le_0", {-0.08929647f, 0.04831043f, 0.19751428f, 0.97502838f}},
	{"j_ring_le_0", {-0.16129128f, -0.15518754f, 0.17969405f, 0.95792066f}},
	{"j_thumb_le_1", {0.24173340f, 0.03045701f, -0.35751885f, 0.90156398f}},
	{"j_index_le_2", {0.02755776f, -0.00634775f, 0.69776383f, 0.71576946f}},
	{"j_mid_le_2", {-0.02893177f, 0.00506611f, 0.59929668f, 0.79998798f}},
	{"j_pinky_le_1", {0.02264484f, 0.01443532f, 0.51442240f, 0.85711634f}},
	{"j_ring_le_1", {0.01263475f, 0.00596641f, 0.73289204f, 0.68020145f}},
	{"j_thumb_le_2", {0.14661076f, -0.22898013f, -0.46775669f, 0.84099766f}},
	{"j_pinky_le_2", {-0.02862632f, 0.01782278f, 0.63658470f, 0.77046921f}},
	{"j_ring_le_2", {-0.00173954f, -0.00259405f, 0.61109617f, 0.79155020f}},
}};
inline const auto& action_fingers=handle_pose_fingers_0;

// Mesh body grab box, receiver release paddle and complete magazine collider.
inline constexpr magazine_contact_profile contacts{{4.22867397f, 1.96326207f, 0.99885731f},{5.47204708f, -0.79360600f, -6.07272584f},{10.02488376f, 0.82540700f, 3.53156281f},{5.30888397f, 0.33768212f, 0.89808990f},strike_regions};
}
