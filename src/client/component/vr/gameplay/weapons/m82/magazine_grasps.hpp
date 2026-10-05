#pragma once
#include "reload_poses.hpp"
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::m82
{
// Receiver-held wrap fitted at native hand scale with coupled finger flexion.
// Receiver h2_viewmodel_m82_base; SHA-256 eab1f4ba514dfe97509d87d5f8df82ea3e6e2873cbd936f9bf09b7506e699b2c.
inline constexpr auto attached_wrap_fingers=[] {
	auto joints=hand_poses::magazine::body_wrap;
	joints[0].rotation={0.51824152f, -0.23248868f, 0.27665815f, 0.77513548f}; // j_index_le_0
	joints[1].rotation={0.49805810f, -0.28801752f, 0.23267068f, 0.78412269f}; // j_mid_le_0
	joints[4].rotation={-0.35785857f, -0.68183122f, 0.23455925f, 0.59331728f}; // j_thumb_le_0
	joints[6].rotation={0.02667255f, -0.00484572f, 0.09611151f, 0.99500134f}; // j_index_le_1
	joints[7].rotation={-0.01413612f, -0.01610793f, 0.30146890f, 0.95323513f}; // j_mid_le_1
	joints[8].rotation={-0.15197505f, 0.06823516f, 0.32295125f, 0.93163836f}; // j_pinky_le_0
	joints[9].rotation={-0.19621569f, -0.04057305f, 0.35558318f, 0.91291502f}; // j_ring_le_0
	joints[10].rotation={0.10535488f, 0.08412200f, -0.22667563f, 0.96459421f}; // j_thumb_le_1
	joints[11].rotation={-0.00599958f, 0.03587645f, 0.11669628f, 0.99250132f}; // j_index_le_2
	joints[12].rotation={-0.02877663f, -0.00592944f, 0.26196629f, 0.96462967f}; // j_mid_le_2
	joints[13].rotation={0.02030096f, 0.01930626f, 0.35191454f, 0.93561279f}; // j_pinky_le_1
	joints[14].rotation={0.00880563f, 0.01084502f, 0.36812768f, 0.92967030f}; // j_ring_le_1
	joints[15].rotation={-0.03435089f, -0.06333375f, 0.04456100f, 0.99640512f}; // j_thumb_le_2
	joints[16].rotation={-0.03309465f, 0.00628161f, 0.31598022f, 0.94816759f}; // j_pinky_le_2
	joints[17].rotation={-0.00067894f, -0.00305145f, 0.28365219f, 0.95892214f}; // j_ring_le_2
	return joints;
}();
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{magazine_in_wrist,contacts.grip_contact,magazine_fingers,magazine_grasp_kind::native},
	{{{2.93536871f, 6.20080307f, 4.44315834f}, {-0.62060352f, 0.37787282f, 0.05249057f, 0.68506068f}},{4.57361213f, 1.12619939f, 1.24615150f},attached_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
