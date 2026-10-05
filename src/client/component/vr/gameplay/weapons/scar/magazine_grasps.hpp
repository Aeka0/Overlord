#pragma once
#include "reload_poses.hpp"
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::scar
{
// Receiver-held wrap fitted at native hand scale with coupled finger flexion.
// Receiver h2_viewmodel_scar_h_base; SHA-256 9062f1d89ff16e6a57afb0c77c86bd097b27a0d81a0a8956894f35816afa78cf.
inline constexpr auto attached_wrap_fingers=[] {
	auto joints=hand_poses::magazine::body_wrap;
	joints[0].rotation={0.51820304f, -0.23254145f, 0.27674593f, 0.77511404f}; // j_index_le_0
	joints[1].rotation={0.49802598f, -0.28807305f, 0.23275811f, 0.78409674f}; // j_mid_le_0
	joints[4].rotation={-0.10687816f, -0.52740819f, 0.29851205f, 0.78823107f}; // j_thumb_le_0
	joints[6].rotation={0.02540376f, -0.00946334f, 0.26968950f, 0.96256567f}; // j_index_le_1
	joints[7].rotation={-0.01674987f, -0.01336926f, 0.46449381f, 0.88531701f}; // j_mid_le_1
	joints[8].rotation={-0.15193103f, 0.06824674f, 0.32304922f, 0.93161073f}; // j_pinky_le_0
	joints[9].rotation={-0.19618885f, -0.04055721f, 0.35568497f, 0.91288184f}; // j_ring_le_0
	joints[10].rotation={0.10290267f, 0.08710454f, -0.25420573f, 0.95770731f}; // j_thumb_le_1
	joints[11].rotation={-0.00184431f, 0.03632785f, 0.22978318f, 0.97256186f}; // j_index_le_2
	joints[12].rotation={-0.02926686f, -0.00258911f, 0.37089678f, 0.92820920f}; // j_mid_le_2
	joints[13].rotation={0.02338130f, 0.01543294f, 0.51105161f, 0.85909335f}; // j_pinky_le_1
	joints[14].rotation={0.01057650f, 0.00912640f, 0.52596617f, 0.85039075f}; // j_ring_le_1
	joints[15].rotation={-0.03313541f, -0.06397806f, 0.02552875f, 0.99707429f}; // j_thumb_le_2
	joints[16].rotation={-0.03215556f, 0.01003668f, 0.42266562f, 0.90565946f}; // j_pinky_le_2
	joints[17].rotation={-0.00102451f, -0.00295342f, 0.39178475f, 0.92005160f}; // j_ring_le_2
	return joints;
}();
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{magazine_in_wrist,contacts.grip_contact,magazine_fingers,magazine_grasp_kind::native},
	{{{5.28287882f, 4.81591788f, 1.85602992f}, {-0.62439156f, 0.40521584f, 0.09573455f, 0.66088592f}},{4.57973387f, 1.36261612f, 1.12896556f},attached_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
