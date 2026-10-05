#pragma once
#include "handle_poses.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::scar
{
// h2_wpn_asl_scar_h_reload frame 44: ce14f8ede0913bfee23ac9d573202ec86a2b67eb8d3f17bf6dadd01f564ebdd7
// h2_wpn_asl_scar_h_pullout_first frame 17: 72ba6315e99a1eb95ae985fbe68114543357536bbaae035383b9f1b3a1912cb1
// Handle contact retains the real receiver side; native le hand retargeted to left.
inline constexpr hands::anchor magazine_rest={{5.80692817f, 0.00000000f, 3.28079396f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{9.35960014f, 0.16290008f, 4.84100134f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{5.92765400f, 0.75039625f, 5.95895737f}, {-0.02898935f, 0.32724108f, -0.05893294f, 0.94265572f}};
inline constexpr hands::anchor magazine_well={{5.62460375f, -0.00019692f, 0.59055118f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={-0.18232442f, -0.00019692f, 0.01576572f};
inline constexpr hands::vec action_grab_low={9.08512592f, 1.43857211f, 4.56755138f};
inline constexpr hands::vec action_grab_high={9.63630687f, 1.49791806f, 5.11873233f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.61827232f, -0.30844690f, 0.23376775f, 0.68407053f}},
	{"j_mid_le_0", {0.64773799f, -0.27336947f, 0.22067565f, 0.67602285f}},
	{"j_pinkypalm_le", {0.68992689f, -0.23193897f, -0.19034255f, 0.65876772f}},
	{"j_ringpalm_le", {0.71474651f, -0.11459749f, -0.10510619f, 0.68187794f}},
	{"j_thumb_le_0", {-0.37517512f, -0.27890160f, 0.21168267f, 0.85828199f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02670997f, -0.00334485f, 0.17743572f, 0.98376418f}},
	{"j_mid_le_1", {-0.01560096f, -0.01467931f, 0.39021332f, 0.92047525f}},
	{"j_pinky_le_0", {-0.15526646f, 0.17818593f, 0.49279002f, 0.83743662f}},
	{"j_ring_le_0", {-0.09365480f, 0.01101713f, 0.41740595f, 0.90381396f}},
	{"j_thumb_le_1", {0.07692958f, 0.11071473f, -0.49296186f, 0.85954214f}},
	{"j_index_le_2", {-0.01123096f, 0.01495426f, -0.00718718f, 0.99979927f}},
	{"j_mid_le_2", {-0.02929768f, -0.00263069f, 0.37032266f, 0.92843732f}},
	{"j_pinky_le_1", {0.02388609f, 0.01467618f, 0.53744032f, 0.84283567f}},
	{"j_ring_le_1", {0.00988810f, 0.00986313f, 0.46311786f, 0.88618666f}},
	{"j_thumb_le_2", {-0.03129175f, -0.06489274f, -0.00223804f, 0.99739899f}},
	{"j_pinky_le_2", {-0.03348916f, 0.00372328f, 0.24277608f, 0.96949698f}},
	{"j_ring_le_2", {-0.00050355f, -0.00311288f, 0.22752994f, 0.97376598f}},
}};
inline const auto& action_fingers=handle_pose_fingers_0;

inline constexpr magazine_contact_profile contacts{{2.34912821f, 1.22360349f, 1.97847272f},{3.90263280f, -0.54284498f, -3.16979190f},{7.22998672f, 0.54341602f, 3.35067501f},{3.90263280f, 0.00000000f, 0.59055118f},{}};
inline constexpr float action_stroke_m=0.13877125f;
}
