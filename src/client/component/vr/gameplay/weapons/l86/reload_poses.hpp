#pragma once
#include "../../physical_reload_profile.hpp"
#include "../hand_poses/edge_handle.hpp"

namespace vr::gameplay::weapons::l86
{
// Left magazine source: h2_wpn_asl_aug_grip_reload frame 49: 0e94a8a78b06f27abac475f1ce1e06a0cf0eea9da9233c8c7b7f9cd2a3306c7d
// Actual left chains fitted to sagittally reflected native SA80 reload frame 26,
// then shifted 3 cm outward for drum clearance. No right rotations enter left joints.
inline constexpr hands::anchor magazine_rest={{-5.63168789f, 0.00000000f, 1.17705798f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{-4.67806088f, 0.00000000f, 4.07960058f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{4.27970570f, 3.07517731f, 1.58465388f}, {0.87951504f, -0.09964949f, 0.39448215f, -0.24679364f}};
inline constexpr hands::anchor magazine_well={{-6.18552556f, -0.00000850f, 0.00000000f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={-0.55383767f, -0.00000850f, 0.94581178f};
inline constexpr hands::vec action_grab_low={-4.94108463f, -1.70142688f, 3.80969085f};
inline constexpr hands::vec action_grab_high={-4.40587997f, -1.31664389f, 4.34489663f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.69509966f, -0.34370444f, 0.23670500f, 0.58538403f}},
	{"j_mid_le_0", {0.59215271f, -0.43812526f, 0.09900239f, 0.66902911f}},
	{"j_pinkypalm_le", {0.68993177f, -0.23191010f, -0.19034390f, 0.65877238f}},
	{"j_ringpalm_le", {0.71479603f, -0.11456512f, -0.10507398f, 0.68183643f}},
	{"j_thumb_le_0", {-0.21521580f, -0.33365773f, 0.21961045f, 0.89113744f}},
	{"j_webbing_le", {-0.67245194f, -0.03610378f, -0.16803669f, 0.71990873f}},
	{"j_index_le_1", {0.02337716f, -0.01364176f, 0.42799117f, 0.90337754f}},
	{"j_mid_le_1", {-0.01974546f, -0.00845362f, 0.68340053f, 0.72972760f}},
	{"j_pinky_le_0", {-0.16925691f, 0.02200401f, 0.72863483f, 0.66329421f}},
	{"j_ring_le_0", {-0.18024250f, -0.19751599f, 0.57191744f, 0.77550662f}},
	{"j_thumb_le_1", {0.10776221f, 0.08099714f, -0.19825072f, 0.97083645f}},
	{"j_index_le_2", {0.00256352f, 0.03625548f, 0.34512901f, 0.93785123f}},
	{"j_mid_le_2", {-0.02838221f, 0.00747703f, 0.66417422f, 0.74700144f}},
	{"j_pinky_le_1", {0.02108804f, 0.03918531f, 0.66587560f, 0.74473451f}},
	{"j_ring_le_1", {0.01297048f, 0.00500508f, 0.78118918f, 0.62413955f}},
	{"j_thumb_le_2", {-0.02264447f, -0.06842170f, -0.13192997f, 0.98863552f}},
	{"j_pinky_le_2", {-0.02362172f, 0.02404898f, 0.80045266f, 0.59844732f}},
	{"j_ring_le_2", {-0.00164799f, -0.00271612f, 0.58820865f, 0.80870297f}},
}};
// Same index/pinky hooks as M14, registered to the real outer cap of
// the right-side j_reload mesh. Explicit opposite-hand fits keep both wrists
// outside the receiver; the whole hand never reflects to fictitious hardware.
inline const auto action_grips=hand_poses::edge_handle::at(
	{-11.87064651f/2.54f,-4.30235335f/2.54f,10.34166062f/2.54f});
// Physical pull contact; latch/striker points are unused (spare strike disabled).
inline constexpr magazine_contact_profile contacts{{2.55929738f, 2.06821043f, 3.20491975f},{-7.14681580f, -3.60646999f, -2.75590551f},{-3.58376090f, 3.60670202f, 0.00000000f},{-6.18552556f, -0.00000850f, 0.00000000f},{}};
}
