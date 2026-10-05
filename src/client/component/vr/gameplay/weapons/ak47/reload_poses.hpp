#pragma once
#include "magazine_collision.hpp"
#include "../../physical_reload_profile.hpp"
#include "bolt_grips.hpp"

namespace vr::gameplay::weapons::ak47
{
// Reload frame 19: ffcd96501f6f01096054ffb92c993e950fee315369ab6cac8c4c32203d5812c8
// Convert cm to native units once. HMD ergonomics remain pending.
inline constexpr hands::anchor magazine_rest={{6.21705919f, 0.01501200f, 2.37865486f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor bolt_rest={{8.28323514f, -0.28166801f, 3.87379504f}, {0.00000000f, -0.01019327f, 0.00000000f, 0.99994805f}};
inline constexpr hands::anchor magazine_in_wrist={{5.41096873f, 1.66545634f, 0.10204893f}, {-0.75233093f, 0.34773614f, -0.30601705f, 0.46843496f}};
// Exact receiver subsets replace the differently shaped world clip.
inline constexpr hands::anchor magazine_well={{6.21653543f, 0.01496063f, 1.77165354f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={0.00000000f, 0.00000000f, 1.08661417f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.61040930f, -0.16614510f, -0.18198443f, 0.75278015f}},
	{"j_mid_le_0", {0.62171700f, -0.28699326f, 0.02475027f, 0.72834763f}},
	{"j_pinkypalm_le", {0.68993675f, -0.23194229f, -0.19037579f, 0.65874662f}},
	{"j_ringpalm_le", {0.71479603f, -0.11456512f, -0.10507398f, 0.68183643f}},
	{"j_thumb_le_0", {0.05703865f, -0.19836877f, 0.27051396f, 0.94032900f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02060023f, -0.01763990f, 0.58364270f, 0.81155755f}},
	{"j_mid_le_1", {-0.01773122f, -0.01202088f, 0.53310986f, 0.84577478f}},
	{"j_pinky_le_0", {0.00708021f, 0.16824666f, 0.21442065f, 0.96211575f}},
	{"j_ring_le_0", {-0.08560436f, 0.02270575f, 0.24118760f, 0.96642893f}},
	{"j_thumb_le_1", {0.11287097f, 0.07370217f, -0.13370154f, 0.98181059f}},
	{"j_index_le_2", {-0.00520175f, 0.03600873f, 0.13851102f, 0.98969238f}},
	{"j_mid_le_2", {-0.02937217f, 0.00053237f, 0.46807617f, 0.88319969f}},
	{"j_pinky_le_1", {0.01870780f, 0.02084409f, 0.27631389f, 0.96065924f}},
	{"j_ring_le_1", {0.01013221f, 0.00956592f, 0.48573570f, 0.87399466f}},
	{"j_thumb_le_2", {-0.04435339f, -0.05676420f, 0.20920355f, 0.97521509f}},
	{"j_pinky_le_2", {-0.03288192f, 0.00721592f, 0.34287468f, 0.93877775f}},
	{"j_ring_le_2", {-0.00077653f, -0.00302135f, 0.31088044f, 0.95044391f}},
}};
// Body grab box and native j_trigger latch. Spare strikes use the whole
// magazine body; no controller-origin or corner-only overlap test.
inline constexpr magazine_contact_profile contacts{{2.88741682f, -0.46619356f, 1.51110077f},{5.11811024f, -0.82677165f, -4.33070866f},{9.44881890f, 0.82677165f, 0.78740157f},{4.38976378f, 0.00000000f, 0.78740157f},strike_regions};
inline constexpr std::array<std::string_view,2> additional_bullets{"j_bullet02","j_bullet03"};
}
