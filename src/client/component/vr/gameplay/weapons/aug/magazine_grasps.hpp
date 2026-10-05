#pragma once
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::aug
{
// Offline mesh-fitted at native hand scale. Source hand recipes are shared;
// wrist placement and joint corrections belong to this magazine only. HMD fit pending.
// Receiver h2_viewmodel_steyr_base_arctic; SHA-256 2a88b8e7af7b04370ecddf182af06835a5944d6af226b04d149e82735de86bde.
inline constexpr auto bottom_pinch_fingers=[] {
	auto joints=hand_poses::magazine::bottom_pinch;
	joints[0].rotation={0.63359427f, -0.05295558f, -0.07892309f, 0.76780541f}; // j_index_le_0
	joints[1].rotation={0.49024676f, -0.42111966f, 0.33011950f, 0.68799525f}; // j_mid_le_0
	joints[4].rotation={-0.06220405f, -0.09492896f, 0.33108673f, 0.93675009f}; // j_thumb_le_0
	joints[6].rotation={0.02291687f, -0.01449359f, 0.46184076f, 0.88654829f}; // j_index_le_1
	joints[7].rotation={-0.01812003f, -0.01148166f, 0.56027197f, 0.82803089f}; // j_mid_le_1
	joints[10].rotation={0.10421930f, 0.08550591f, -0.23938695f, 0.96152013f}; // j_thumb_le_1
	joints[11].rotation={0.00704605f, 0.03564225f, 0.46026700f, 0.88703680f}; // j_index_le_2
	joints[12].rotation={-0.02938576f, -0.00084844f, 0.42444558f, 0.90497608f}; // j_mid_le_2
	joints[15].rotation={-0.01480810f, -0.07051743f, -0.24145841f, 0.96773232f}; // j_thumb_le_2
	return joints;
}();
inline constexpr auto body_wrap_fingers=[] {
	auto joints=hand_poses::magazine::body_wrap;
	joints[0].rotation={0.53483972f, -0.20854104f, 0.23696162f, 0.78377695f}; // j_index_le_0
	joints[1].rotation={0.51183053f, -0.26276505f, 0.19319010f, 0.79477143f}; // j_mid_le_0
	joints[4].rotation={-0.22485236f, -0.34040978f, 0.36667884f, 0.83612752f}; // j_thumb_le_0
	joints[6].rotation={0.02658249f, -0.00531762f, 0.11372890f, 0.99314190f}; // j_index_le_1
	joints[7].rotation={-0.01441935f, -0.01585490f, 0.31831390f, 0.94774309f}; // j_mid_le_1
	joints[8].rotation={-0.17151376f, 0.06295935f, 0.27863429f, 0.94285846f}; // j_pinky_le_0
	joints[9].rotation={-0.20799710f, -0.04762171f, 0.30951282f, 0.92664513f}; // j_ring_le_0
	joints[10].rotation={0.10759297f, 0.08123986f, -0.20048683f, 0.97037563f}; // j_thumb_le_1
	joints[11].rotation={-0.00558592f, 0.03594318f, 0.12812120f, 0.99109124f}; // j_index_le_2
	joints[12].rotation={-0.02884303f, -0.00559756f, 0.27306052f, 0.96154807f}; // j_mid_le_2
	joints[13].rotation={0.02063990f, 0.01894347f, 0.36843933f, 0.92922957f}; // j_pinky_le_1
	joints[14].rotation={0.00899643f, 0.01068727f, 0.38454462f, 0.92300069f}; // j_ring_le_1
	joints[15].rotation={-0.03548808f, -0.06270361f, 0.06253277f, 0.99543885f}; // j_thumb_le_2
	joints[16].rotation={-0.03302010f, 0.00666241f, 0.32688124f, 0.94446490f}; // j_pinky_le_2
	joints[17].rotation={-0.00071405f, -0.00304343f, 0.29467923f, 0.95559112f}; // j_ring_le_2
	return joints;
}();
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{{{6.64250504f, 2.28681764f, 7.18033397f}, {-0.08926221f, 0.27556159f, -0.01952943f, 0.95693086f}},{5.98162442f, 1.00262487f, 1.51136002f},bottom_pinch_fingers,magazine_grasp_kind::bottom_pinch},
	{{{5.78252256f, 4.24344478f, 2.10040674f}, {-0.62172514f, 0.42027283f, -0.08919835f, 0.65488338f}},{5.14853221f, 1.37278078f, 1.30192068f},body_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
