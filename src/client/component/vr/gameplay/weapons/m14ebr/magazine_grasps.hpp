#pragma once
#include "reload_poses.hpp"
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::m14ebr
{
// Receiver-held wrap fitted at native hand scale with coupled finger flexion.
// Receiver h2_viewmodel_m14ebr_base; SHA-256 630604208cd35ec7c7ed24dded5adb02a2336b8fc54ad95768dfd88fa1a6be9b.
inline constexpr auto attached_wrap_fingers=[] {
	auto joints=hand_poses::magazine::body_wrap;
	joints[0].rotation={0.49608773f, -0.26109993f, 0.32445704f, 0.76187362f}; // j_index_le_0
	joints[1].rotation={0.47944322f, -0.31804114f, 0.28035482f, 0.76836528f}; // j_mid_le_0
	joints[4].rotation={-0.17808095f, -0.63728136f, 0.24088146f, 0.71002519f}; // j_thumb_le_0
	joints[6].rotation={0.02640754f, -0.00612760f, 0.14403008f, 0.98920192f}; // j_index_le_1
	joints[7].rotation={-0.01489720f, -0.01540678f, 0.34713193f, 0.93757140f}; // j_mid_le_1
	joints[8].rotation={-0.12746298f, 0.07448020f, 0.37626546f, 0.91467491f}; // j_pinky_le_0
	joints[9].rotation={-0.18107783f, -0.03177897f, 0.41093887f, 0.89293346f}; // j_ring_le_0
	joints[10].rotation={0.10198338f, 0.08817909f, -0.26423704f, 0.95498829f}; // j_thumb_le_1
	joints[11].rotation={-0.00487068f, 0.03604706f, 0.14778724f, 0.98835005f}; // j_index_le_2
	joints[12].rotation={-0.02894855f, -0.00502340f, 0.29211098f, 0.95593301f}; // j_mid_le_2
	joints[13].rotation={0.02120925f, 0.01830378f, 0.39666810f, 0.91753450f}; // j_pinky_le_1
	joints[14].rotation={0.00931888f, 0.01040731f, 0.41257549f, 0.91081629f}; // j_ring_le_1
	joints[15].rotation={-0.03268722f, -0.06420820f, 0.01855591f, 0.99722842f}; // j_thumb_le_2
	joints[16].rotation={-0.03288121f, 0.00731715f, 0.34558166f, 0.93778388f}; // j_pinky_le_2
	joints[17].rotation={-0.00077437f, -0.00302864f, 0.31360707f, 0.94954770f}; // j_ring_le_2
	return joints;
}();
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{magazine_in_wrist,contacts.grip_contact,magazine_fingers,magazine_grasp_kind::native},
	{{{3.77752046f, 3.12088269f, 2.48650611f}, {-0.62865199f, 0.39765462f, 0.06932570f, 0.66472658f}},{4.37204294f, 1.45384760f, 1.24572818f},attached_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
