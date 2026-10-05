#pragma once
#include "reload_poses.hpp"
namespace vr::gameplay::weapons::pp2000
{
// h2_wpn_pst_pp2000_first_time_pullout frame 15; SHA-256 da51467cfa97be842ef70797ada21d1a104871b4f74d4c38cceb5f4474417568.
// j_reload_end alone unfolds about local Z; the rod keeps native recoil.
inline constexpr hands::vec handle_symmetry={5.69892603f, 0.00950003f, 3.48800518f};
inline constexpr charging_handle_fold handle_fold{{0.00000000f, 0.00000000f, 0.76047885f, 0.64936270f},"j_reload_end",{},hands::quat{0.00000000f, 0.00000000f, -0.76047885f, 0.64936270f}};
inline constexpr std::array<joint_pose,18> folding_fingers{{
	{"j_index_le_0",{0.66265167f, -0.15543154f, 0.01446584f, 0.73247836f}},
	{"j_mid_le_0",{0.43150140f, -0.47230467f, 0.49623119f, 0.58693224f}},
	{"j_pinkypalm_le",{0.64742803f, -0.27879019f, -0.14755890f, 0.69378624f}},
	{"j_ringpalm_le",{0.68057139f, -0.16660266f, -0.05694826f, 0.71121237f}},
	{"j_thumb_le_0",{-0.14719020f, -0.31070774f, 0.17953970f, 0.92171646f}},
	{"j_webbing_le",{-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1",{0.01855532f, -0.01983710f, 0.67082958f, 0.74111394f}},
	{"j_mid_le_1",{-0.01901330f, -0.00988813f, 0.62518036f, 0.78018603f}},
	{"j_pinky_le_0",{0.01236008f, 0.16995880f, 0.78497220f, 0.59563401f}},
	{"j_ring_le_0",{-0.06439349f, 0.02468926f, 0.74818527f, 0.65989599f}},
	{"j_thumb_le_1",{-0.00144454f, 0.24715874f, -0.40586484f, 0.87987738f}},
	{"j_index_le_2",{0.01309252f, 0.03390626f, 0.60570438f, 0.79485920f}},
	{"j_mid_le_2",{-0.02932822f, 0.00177007f, 0.50559906f, 0.86226812f}},
	{"j_pinky_le_1",{0.02597101f, 0.01065086f, 0.66584672f, 0.74556033f}},
	{"j_ring_le_1",{0.01211601f, 0.00692779f, 0.67886276f, 0.73413252f}},
	{"j_thumb_le_2",{-0.03167789f, -0.06466807f, 0.00399788f, 0.99739589f}},
	{"j_pinky_le_2",{-0.03030486f, 0.01474043f, 0.55339171f, 0.83223912f}},
	{"j_ring_le_2",{-0.00149542f, -0.00280772f, 0.52498333f, 0.85110656f}},
}};
inline constexpr std::array<part_grip_pose,1> folding_grips{{{"folding_tip",{{1.82201432f, 4.92146200f, 3.36861728f}, {0.94648616f, -0.30559787f, 0.08073913f, -0.06523102f}},{7.44126791f, 0.96249390f, 0.66887117f},folding_fingers,&handle_symmetry}}};
}
