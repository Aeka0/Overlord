#pragma once
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::acr
{
// Offline mesh-fitted at native hand scale. Source hand recipes are shared;
// wrist placement and joint corrections belong to this magazine only. HMD fit pending.
// Receiver h2_viewmodel_magpul_masada_base; SHA-256 708f880ad6facbacf7f4f4f2499f8cb97aea5b44c39211d2b2331fd4ba84cecc.
inline constexpr auto bottom_pinch_fingers=[] {
	auto joints=hand_poses::magazine::bottom_pinch;
	joints[0].rotation={0.51984847f, -0.18080040f, -0.19987990f, 0.81062741f}; // j_index_le_0
	joints[1].rotation={0.49062610f, -0.46780256f, 0.35515554f, 0.64367021f}; // j_mid_le_0
	joints[4].rotation={-0.08665712f, -0.10087036f, 0.35132107f, 0.92676276f}; // j_thumb_le_0
	joints[6].rotation={0.02384173f, -0.01291584f, 0.40102970f, 0.91566365f}; // j_index_le_1
	joints[7].rotation={-0.01712240f, -0.01292235f, 0.49081642f, 0.87099890f}; // j_mid_le_1
	joints[10].rotation={0.09862017f, 0.09190748f, -0.29954096f, 0.94451167f}; // j_thumb_le_1
	joints[11].rotation={0.00349963f, 0.03616309f, 0.37061983f, 0.92807377f}; // j_index_le_2
	joints[12].rotation={-0.02932714f, -0.00203999f, 0.38737934f, 0.92145158f}; // j_mid_le_2
	joints[15].rotation={-0.01790202f, -0.06979617f, -0.19856750f, 0.97743489f}; // j_thumb_le_2
	return joints;
}();
inline constexpr auto body_wrap_fingers=[] {
	auto joints=hand_poses::magazine::body_wrap;
	joints[0].rotation={0.53483972f, -0.20854104f, 0.23696162f, 0.78377695f}; // j_index_le_0
	joints[1].rotation={0.51183053f, -0.26276505f, 0.19319010f, 0.79477143f}; // j_mid_le_0
	joints[4].rotation={-0.23029999f, -0.33674815f, 0.38008354f, 0.83011993f}; // j_thumb_le_0
	joints[6].rotation={0.02667255f, -0.00484572f, 0.09611151f, 0.99500134f}; // j_index_le_1
	joints[7].rotation={-0.01413612f, -0.01610793f, 0.30146890f, 0.95323513f}; // j_mid_le_1
	joints[8].rotation={-0.17151376f, 0.06295935f, 0.27863429f, 0.94285846f}; // j_pinky_le_0
	joints[9].rotation={-0.20799710f, -0.04762171f, 0.30951282f, 0.92664513f}; // j_ring_le_0
	joints[10].rotation={0.10817834f, 0.08045874f, -0.19345592f, 0.97180175f}; // j_thumb_le_1
	joints[11].rotation={-0.00599958f, 0.03587645f, 0.11669628f, 0.99250132f}; // j_index_le_2
	joints[12].rotation={-0.02877663f, -0.00592944f, 0.26196629f, 0.96462967f}; // j_mid_le_2
	joints[13].rotation={0.02030096f, 0.01930626f, 0.35191454f, 0.93561279f}; // j_pinky_le_1
	joints[14].rotation={0.00880563f, 0.01084502f, 0.36812768f, 0.92967030f}; // j_ring_le_1
	joints[15].rotation={-0.03579032f, -0.06253159f, 0.06733681f, 0.99512542f}; // j_thumb_le_2
	joints[16].rotation={-0.03309465f, 0.00628161f, 0.31598022f, 0.94816759f}; // j_pinky_le_2
	joints[17].rotation={-0.00067894f, -0.00305145f, 0.28365219f, 0.95892214f}; // j_ring_le_2
	return joints;
}();
// Accepted wrap lowered 2 cm along the measured X/Z centreline; rotate the
// complete hand with the curve tangent, preserving all finger articulation.
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{{{8.24766192f, 2.08634206f, 9.37192023f}, {-0.06169304f, 0.28208318f, -0.03729766f, 0.95667755f}},{6.31913905f, 0.44795405f, 1.39122659f},bottom_pinch_fingers,magazine_grasp_kind::bottom_pinch},
	{{{8.11681394f, 5.32017753f, 3.26300127f}, {-0.60164795f, 0.30886769f, -0.29802788f, 0.67364670f}},{6.28093225f, 1.96072999f, 2.46529858f},body_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
