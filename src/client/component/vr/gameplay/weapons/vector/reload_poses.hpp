#pragma once
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::vector
{
// h2_wpn_smg_kriss_reload frame 36: 149ce453bd28dfad358956729cc58ea62b6ec73bf8182dc3585c303db4f25c01
// h2_wpn_smg_kriss_reload_empty frame 61: 60b65b11e3e36d9fd2b4931e4da6990f8f9f9acb45a8b0a62c19d6aab17c18ec
// Native grasp transformed with j_reload to its forward, 90-degree unfolded
// pose; nearest reference hand/handle skin is 1.330mm. HMD pending.
// Acquisition contact maps back to the same vertex of the folded handle.
inline constexpr hands::anchor magazine_rest={{8.00567687f, 0.19266299f, 1.03309502f}, {0.00000000f, 0.22156175f, 0.00000000f, 0.97514634f}};
inline constexpr hands::anchor action_rest={{9.54958623f, 0.42315797f, 1.75429498f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{7.24132915f, 2.43508041f, 8.89625906f}, {-0.17728230f, 0.18977196f, -0.28394938f, 0.92300072f}};
inline constexpr hands::anchor action_wrist={{4.61096335f, 4.09504971f, 1.02248656f}, {0.95286258f, -0.17461860f, 0.18187245f, -0.16877104f}};
inline constexpr hands::anchor magazine_well={{6.30445537f, 0.00000016f, -2.51738050f}, {0.00000000f, 0.22156175f, 0.00000000f, 0.97514634f}};
// Well is at the grip mouth, above the protruding magazine baseplate.
inline constexpr hands::vec magazine_top={0.00000000f, -0.19266283f, 0.01574803f};
inline constexpr hands::vec action_grab_low={9.33070866f, -0.07874016f, 1.57480315f}, action_grab_high={12.04724409f, 1.22047244f, 1.96850394f};
inline constexpr hands::vec action_contact={6.74494959f, 0.92503533f, 1.16965425f};
inline constexpr std::array<joint_pose,15> magazine_fingers{{
	{"j_index_le_0", {0.65205343f, -0.33966356f, 0.07309126f, 0.67387881f}},
	{"j_mid_le_0", {0.56426563f, -0.44850709f, 0.14084514f, 0.67868132f}},
	{"j_thumb_le_0", {-0.21445421f, -0.16516667f, 0.09015805f, 0.95843669f}},
	{"j_index_le_1", {-0.01406902f, -0.02447582f, 0.55580243f, 0.83083491f}},
	{"j_mid_le_1", {-0.01971519f, -0.00846390f, 0.68134378f, 0.73164904f}},
	{"j_pinky_le_0", {-0.10297977f, 0.16257233f, 0.76880302f, 0.60984205f}},
	{"j_ring_le_0", {-0.16763748f, 0.01275668f, 0.60835345f, 0.77565522f}},
	{"j_thumb_le_1", {0.11157453f, 0.07568513f, -0.15079041f, 0.97932892f}},
	{"j_index_le_2", {0.06637735f, 0.01123074f, -0.02450621f, 0.99743038f}},
	{"j_mid_le_2", {-0.02880960f, -0.00580999f, 0.26771339f, 0.96305025f}},
	{"j_pinky_le_1", {0.02633734f, 0.00955224f, 0.69757835f, 0.71596057f}},
	{"j_ring_le_1", {0.01216672f, 0.00679546f, 0.68509027f, 0.72832487f}},
	{"j_thumb_le_2", {-0.02072814f, -0.06894732f, -0.15863499f, 0.98470887f}},
	{"j_pinky_le_2", {-0.02951149f, 0.01616468f, 0.59400384f, 0.80375818f}},
	{"j_ring_le_2", {-0.00127161f, -0.00284840f, 0.46224423f, 0.88674717f}},
}};
inline constexpr std::array<joint_pose,15> action_fingers{{
	{"j_index_le_0", {0.59318157f, -0.13983423f, -0.03680491f, 0.79197690f}},
	{"j_mid_le_0", {0.57164054f, -0.19510426f, -0.00439465f, 0.79695804f}},
	{"j_thumb_le_0", {-0.01681562f, -0.38352441f, 0.17953959f, 0.90575482f}},
	{"j_index_le_1", {0.03216642f, -0.06356989f, 0.63566834f, 0.76866765f}},
	{"j_mid_le_1", {0.01538130f, 0.00466932f, 0.71452839f, 0.69942176f}},
	{"j_pinky_le_0", {-0.08108731f, 0.09518680f, 0.66841336f, 0.73320386f}},
	{"j_ring_le_0", {-0.15341965f, -0.05743700f, 0.65075935f, 0.74140116f}},
	{"j_thumb_le_1", {0.08516055f, 0.10449719f, -0.42557386f, 0.89482675f}},
	{"j_index_le_2", {0.00650047f, 0.04400786f, 0.39329359f, 0.91833611f}},
	{"j_mid_le_2", {-0.02938929f, 0.00112918f, 0.48417561f, 0.87447640f}},
	{"j_pinky_le_1", {0.02407941f, 0.01434388f, 0.54940114f, 0.83508851f}},
	{"j_ring_le_1", {0.01190226f, 0.00729395f, 0.65483768f, 0.75564062f}},
	{"j_thumb_le_2", {-0.00640888f, -0.07171838f, -0.35386155f, 0.93252207f}},
	{"j_pinky_le_2", {-0.03125094f, 0.01275673f, 0.49769560f, 0.86669472f}},
	{"j_ring_le_2", {-0.00137335f, -0.00286878f, 0.48271745f, 0.87577037f}},
}};
inline constexpr std::array<part_grip_pose,1> action_grips{{{"folding_side_handle",action_wrist,action_contact,action_fingers}}};
}
