#pragma once
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::aa12
{
// h2_wpn_sho_aa12_reload frame 50: 2db0e1b717e477341af0fc80640422786f6c472db7047816bbeaccf56a5597f1
// h2_wpn_sho_aa12_first_pullout frame 17: dff9f02a605e00ff13d56795bdd4b8672d91ca7f0d302c76ee47ec8810bbb751
// Handle contact retains the real receiver side; native le hand retargeted to left.
inline constexpr hands::anchor magazine_rest={{6.60637683f, 0.00000000f, 3.22090284f}, {0.00000000f, -0.02792463f, 0.00000000f, 0.99961003f}};
inline constexpr hands::anchor action_rest={{9.83727598f, 0.00000000f, 6.34494016f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{3.91782338f, 4.06677294f, 1.16645388f}, {-0.67396993f, 0.25292739f, -0.07295903f, 0.69026752f}};
inline constexpr hands::anchor magazine_well={{6.46503313f, 0.00213634f, 1.18110236f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_wrist={{9.33075590f, 4.33135719f, 9.49852491f}, {0.70489335f, -0.60636986f, 0.14525452f, -0.33814506f}};
inline constexpr hands::vec magazine_top={-0.09890476f, 0.00213634f, 0.76294305f};
inline constexpr hands::vec action_grab_low={9.81338644f, -0.18607799f, 6.73322970f};
inline constexpr hands::vec action_grab_high={11.23455566f, 0.19184200f, 7.61782570f};
inline constexpr hands::vec action_contact={4.67561746f, 1.05291898f, 1.17981598f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.65685872f, -0.41096778f, 0.23237106f, 0.58791648f}},
	{"j_mid_le_0", {0.53739817f, -0.48813383f, 0.25674992f, 0.63797183f}},
	{"j_pinkypalm_le", {0.65223588f, -0.14042428f, -0.17736921f, 0.72347048f}},
	{"j_ringpalm_le", {0.68621004f, -0.10364106f, -0.10873766f, 0.71172356f}},
	{"j_thumb_le_0", {-0.24094209f, -0.02871773f, 0.43427561f, 0.86748308f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02686682f, -0.03484126f, 0.60762006f, 0.79300827f}},
	{"j_mid_le_1", {-0.01881006f, -0.01031525f, 0.61079680f, 0.79149671f}},
	{"j_pinky_le_0", {-0.00805699f, 0.00637845f, 0.59878108f, 0.80084681f}},
	{"j_ring_le_0", {-0.13328058f, -0.01212612f, 0.61480553f, 0.77724089f}},
	{"j_thumb_le_1", {-0.05819921f, 0.19174914f, -0.30582813f, 0.93076005f}},
	{"j_index_le_2", {0.02451713f, 0.08335550f, 0.55008741f, 0.83057487f}},
	{"j_mid_le_2", {-0.02782231f, 0.00948061f, 0.71523451f, 0.69826616f}},
	{"j_pinky_le_1", {0.06132286f, 0.01918562f, 0.50886972f, 0.85844221f}},
	{"j_ring_le_1", {0.04727316f, 0.00036622f, 0.67092947f, 0.74001268f}},
	{"j_thumb_le_2", {-0.24475901f, -0.02160715f, -0.12714040f, 0.96096903f}},
	{"j_pinky_le_2", {-0.01745658f, 0.00875881f, 0.61493720f, 0.78833419f}},
	{"j_ring_le_2", {-0.00153658f, -0.00274662f, 0.55021878f, 0.83501460f}},
}};
inline constexpr std::array<joint_pose, 18> action_fingers{{
	{"j_index_le_0", {0.64574594f, -0.31367768f, -0.04486865f, 0.69469799f}},
	{"j_mid_le_0", {0.56691593f, -0.43638732f, 0.10046766f, 0.69143235f}},
	{"j_pinkypalm_le", {0.63427412f, -0.28726722f, -0.04512063f, 0.71633652f}},
	{"j_ringpalm_le", {0.69889922f, -0.15622310f, -0.01110866f, 0.69786160f}},
	{"j_thumb_le_0", {0.16527287f, -0.10469367f, 0.23082658f, 0.95312287f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.04776151f, -0.03146461f, 0.59784284f, 0.79957036f}},
	{"j_mid_le_1", {-0.02085446f, -0.00534078f, 0.78867494f, 0.61443340f}},
	{"j_pinky_le_0", {-0.18550557f, 0.09031643f, 0.71823517f, 0.66450648f}},
	{"j_ring_le_0", {-0.17432977f, -0.03240692f, 0.69753654f, 0.69426342f}},
	{"j_thumb_le_1", {0.12857615f, 0.09437972f, -0.27769152f, 0.94733735f}},
	{"j_index_le_2", {0.01878403f, 0.03112870f, 0.73437789f, 0.67776639f}},
	{"j_mid_le_2", {-0.02934854f, -0.00134281f, 0.41177480f, 0.91081193f}},
	{"j_pinky_le_1", {0.02698965f, 0.00740495f, 0.75130160f, 0.65936533f}},
	{"j_ring_le_1", {0.01185244f, 0.00734396f, 0.65292203f, 0.75729678f}},
	{"j_thumb_le_2", {0.00502024f, -0.05967828f, -0.13940700f, 0.98842247f}},
	{"j_pinky_le_2", {-0.02616143f, 0.02122077f, 0.72742335f, 0.68536160f}},
	{"j_ring_le_2", {-0.00217699f, -0.00229906f, 0.72871370f, 0.68481116f}},
}};
inline constexpr std::array<part_grip_pose,1> action_grips{{{"charging_handle",action_wrist,action_contact,action_fingers}}};
inline constexpr magazine_contact_profile contacts{{2.94455797f, 2.74492629f, 0.79004407f},{4.73920492f, -0.68094097f, -4.71917176f},{8.89629385f, 0.68521303f, 4.11635573f},{4.73920492f, 0.00000000f, 1.18110236f},{}};
inline constexpr float action_stroke_m=0.10185759f;
}
