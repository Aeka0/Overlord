#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::rpd
{
// h2_wpn_lmg_rpd_reload frame 17; j_rail_release contact 0.473 mm.
// SHA256 1f923260472fd933065c580299dcea0630f1e99096e3b64f247c7f12846fbc3c
inline constexpr hands::anchor bridge_release_wrist={{-3.53925267f, 5.02817497f, 0.23281951f}, {0.82668924f, -0.40140648f, 0.38813528f, 0.06934509f}};
inline constexpr hands::vec bridge_release_contact={5.43803025f, 0.34377286f, 1.36305452f};
inline constexpr std::array<joint_pose, 18> bridge_release_fingers{{
	{"j_index_le_0", {0.61757261f, -0.10049746f, -0.10153509f, 0.77343064f}},
	{"j_mid_le_0", {0.40989360f, -0.45506094f, 0.17593899f, 0.77068298f}},
	{"j_pinkypalm_le", {0.68421426f, -0.19642715f, -0.18273452f, 0.67814107f}},
	{"j_ringpalm_le", {0.71834374f, -0.10730293f, -0.10868182f, 0.67871689f}},
	{"j_thumb_le_0", {0.07449639f, -0.13022372f, 0.27292964f, 0.95026390f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.20404802f, -0.14478071f, 0.49760739f, 0.83053587f}},
	{"j_mid_le_1", {0.00360120f, -0.03448605f, 0.82336205f, 0.56645624f}},
	{"j_pinky_le_0", {-0.22318466f, 0.00747713f, 0.69534245f, 0.68310437f}},
	{"j_ring_le_0", {-0.27268073f, -0.10486077f, 0.59470950f, 0.74897934f}},
	{"j_thumb_le_1", {0.11450578f, 0.09317328f, -0.40711808f, 0.90136676f}},
	{"j_index_le_2", {0.03234929f, 0.03686598f, 0.62797293f, 0.77668811f}},
	{"j_mid_le_2", {-0.03894197f, 0.01086469f, 0.71706936f, 0.69582829f}},
	{"j_pinky_le_1", {0.02539129f, 0.01178009f, 0.63237125f, 0.77415962f}},
	{"j_ring_le_1", {0.03399710f, 0.02072175f, 0.76304254f, 0.64512083f}},
	{"j_thumb_le_2", {0.00109867f, -0.02298054f, -0.23300991f, 0.97220218f}},
	{"j_pinky_le_2", {-0.04321391f, 0.01998948f, 0.75355780f, 0.65565510f}},
	{"j_ring_le_2", {-0.01611371f, -0.00787375f, 0.64659321f, 0.76262414f}},
}};
inline const part_grip_pose bridge_release_grip{"bridge_release",bridge_release_wrist,bridge_release_contact,bridge_release_fingers};
// h2_wpn_lmg_rpd_reload frame 201; j_rail contact 0.894 mm.
// SHA256 1f923260472fd933065c580299dcea0630f1e99096e3b64f247c7f12846fbc3c
inline constexpr hands::anchor bridge_return_wrist={{-2.27452212f, 3.23980095f, 7.09746414f}, {0.98064910f, -0.06213955f, -0.17120454f, 0.07179848f}};
inline constexpr hands::vec bridge_return_contact={4.38884411f, 2.54440975f, 2.17087406f};
inline constexpr std::array<joint_pose, 18> bridge_return_fingers{{
	{"j_index_le_0", {0.70948136f, -0.13861254f, -0.11377093f, 0.68152692f}},
	{"j_mid_le_0", {0.61892310f, -0.21561596f, 0.03891159f, 0.75427438f}},
	{"j_pinkypalm_le", {0.68753053f, -0.21674547f, -0.18729470f, 0.66726596f}},
	{"j_ringpalm_le", {0.71629624f, -0.11140577f, -0.10647045f, 0.68056778f}},
	{"j_thumb_le_0", {-0.01052888f, -0.23410727f, 0.24539911f, 0.94067115f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.09756845f, -0.12732423f, 0.15445540f, 0.97489100f}},
	{"j_mid_le_1", {0.01779215f, -0.05127068f, 0.44224016f, 0.89525326f}},
	{"j_pinky_le_0", {-0.21631365f, 0.09228732f, 0.34558914f, 0.90843800f}},
	{"j_ring_le_0", {-0.19104580f, -0.03280739f, 0.24723035f, 0.94936944f}},
	{"j_thumb_le_1", {0.14560467f, 0.11191204f, -0.19354954f, 0.96374973f}},
	{"j_index_le_2", {0.00341809f, 0.03003034f, 0.21860502f, 0.97534524f}},
	{"j_mid_le_2", {-0.03399744f, -0.00317391f, 0.18042447f, 0.98299599f}},
	{"j_pinky_le_1", {0.02429268f, 0.03811754f, 0.31052518f, 0.94948988f}},
	{"j_ring_le_1", {0.01977603f, 0.02575767f, 0.44132900f, 0.89675758f}},
	{"j_thumb_le_2", {-0.00715775f, -0.01611639f, -0.05008894f, 0.99858907f}},
	{"j_pinky_le_2", {-0.04223715f, 0.02902278f, 0.19009768f, 0.98042673f}},
	{"j_ring_le_2", {-0.00766026f, 0.00753819f, 0.13757956f, 0.99043241f}},
}};
inline const part_grip_pose bridge_return_grip{"bridge_return",bridge_return_wrist,bridge_return_contact,bridge_return_fingers};
}
