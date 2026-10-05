#pragma once
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::miniuzi
{
// h2_wpn_smg_miniuzi_reload frame 42: b86ae131b6fb484a960503ff4b7cfbe61f76941d15170f105cc0a126048a2d26
// h2_wpn_smg_miniuzi_pullout_first frame 22: 1727b6468940e7a7b6df11a97bd98efe49c36c88d653f81dd6715c25086dd3d1
// Rearward fit 0.974288 cm; nearest action contact 0.647 mm. HMD pending.
inline constexpr hands::anchor magazine_rest={{0.44872601f, 0.01501200f, 2.84736889f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{3.13378488f, 0.00000000f, 4.34067118f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{7.66834873f, 5.67671179f, 2.34010607f}, {-0.59742708f, 0.18065122f, -0.22090469f, 0.74943121f}};
inline constexpr hands::anchor action_wrist={{0.66321543f, 2.20824559f, 7.72817626f}, {0.83092502f, -0.36639498f, -0.12407137f, 0.39990577f}};
inline constexpr hands::anchor magazine_well={{-0.11076892f, 0.00000000f, -2.27074135f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
// Closed bolt uses receiver bind; native idle already holds it 52.513mm rearward.
inline constexpr hands::anchor bolt_rest={{1.52304492f, -0.56304598f, 3.59577382f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
// Well is at the grip mouth, above the protruding magazine baseplate.
inline constexpr hands::vec magazine_top={-0.55949493f, -0.01501200f, -0.12479029f};
inline constexpr hands::vec action_grab_low={1.96850394f, -0.66929134f, 4.17322835f}, action_grab_high={4.05511811f, 0.66929134f, 5.19685039f};
inline constexpr hands::vec action_contact={3.54398240f, -2.49898314f, 2.18780596f};
inline constexpr std::array<joint_pose,15> magazine_fingers{{
	{"j_index_le_0", {0.69707678f, -0.24701806f, 0.04022360f, 0.67189888f}},
	{"j_mid_le_0", {0.59486936f, -0.35477612f, 0.15497564f, 0.70444794f}},
	{"j_thumb_le_0", {0.01331046f, -0.13744693f, 0.35599479f, 0.92422881f}},
	{"j_index_le_1", {0.02035584f, -0.01786659f, 0.59404917f, 0.80397264f}},
	{"j_mid_le_1", {-0.02005072f, -0.00769069f, 0.71083930f, 0.70302654f}},
	{"j_pinky_le_0", {-0.20876693f, 0.16491185f, 0.39201022f, 0.88065228f}},
	{"j_ring_le_0", {-0.17593432f, -0.04316612f, 0.33081095f, 0.92614681f}},
	{"j_thumb_le_1", {0.09272885f, 0.09784383f, -0.35784718f, 0.92399856f}},
	{"j_index_le_2", {0.00042726f, 0.03637828f, 0.28950030f, 0.95648629f}},
	{"j_mid_le_2", {-0.02932815f, 0.00219732f, 0.51713367f, 0.85539921f}},
	{"j_pinky_le_1", {0.02690876f, 0.00798735f, 0.73933912f, 0.67274794f}},
	{"j_ring_le_1", {0.01278731f, 0.00552387f, 0.75381045f, 0.65694428f}},
	{"j_thumb_le_2", {-0.04717455f, -0.05442513f, 0.25869388f, 0.96327044f}},
	{"j_pinky_le_2", {-0.02986445f, 0.01562553f, 0.57890092f, 0.81470098f}},
	{"j_ring_le_2", {-0.00154582f, -0.00271613f, 0.57169328f, 0.82046147f}},
}};
inline constexpr std::array<joint_pose,15> action_fingers{{
	{"j_index_le_0", {0.59383947f, -0.39070474f, 0.44493707f, 0.54473433f}},
	{"j_mid_le_0", {0.59398458f, -0.38841092f, 0.35844160f, 0.60649723f}},
	{"j_thumb_le_0", {-0.07179982f, -0.37178633f, 0.22275441f, 0.89833189f}},
	{"j_index_le_1", {0.01713115f, -0.02106805f, 0.71801271f, 0.69550011f}},
	{"j_mid_le_1", {-0.01974509f, -0.00851450f, 0.67987812f, 0.73300981f}},
	{"j_pinky_le_0", {0.00056460f, 0.23104331f, 0.29845955f, 0.92603486f}},
	{"j_ring_le_0", {-0.03674443f, 0.04156638f, 0.35416873f, 0.93353446f}},
	{"j_thumb_le_1", {0.08926702f, 0.10101669f, -0.38950938f, 0.91110343f}},
	{"j_index_le_2", {0.01534071f, 0.03299066f, 0.65740218f, 0.75266105f}},
	{"j_mid_le_2", {-0.02908405f, 0.00387584f, 0.56654392f, 0.82350900f}},
	{"j_pinky_le_1", {0.02112902f, 0.01836201f, 0.39305885f, 0.91908712f}},
	{"j_ring_le_1", {0.01208545f, 0.00695829f, 0.67535068f, 0.73736489f}},
	{"j_thumb_le_2", {0.00210580f, -0.07202435f, -0.46153572f, 0.88419050f}},
	{"j_pinky_le_2", {-0.02917534f, 0.01684601f, 0.61207187f, 0.79008420f}},
	{"j_ring_le_2", {-0.00128178f, -0.00289925f, 0.47041162f, 0.88244142f}},
}};
inline constexpr std::array<part_grip_pose,1> action_grips{{{"top_handle",action_wrist,action_contact,action_fingers}}};
}
