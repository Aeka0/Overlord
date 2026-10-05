#pragma once
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::fal
{
// Offline mesh-fitted at native hand scale. Source hand recipes are shared;
// wrist placement and joint corrections belong to this magazine only. HMD fit pending.
// Receiver h2_viewmodel_fn_fal_base; SHA-256 7dc28d5381843b0dfe3a643bd7b60a222fcc5720d215942bd4c1073bcf990144.
inline constexpr auto bottom_pinch_fingers=[] {
	auto joints=hand_poses::magazine::bottom_pinch;
	joints[0].rotation={0.68639827f, 0.02115754f, -0.02545940f, 0.72647202f}; // j_index_le_0
	joints[1].rotation={0.47411763f, -0.37056756f, 0.44777625f, 0.66135360f}; // j_mid_le_0
	joints[4].rotation={-0.16813306f, -0.10966558f, 0.37054988f, 0.90686136f}; // j_thumb_le_0
	joints[6].rotation={0.02313877f, -0.01413663f, 0.44804357f, 0.89360042f}; // j_index_le_1
	joints[7].rotation={-0.01613300f, -0.01413824f, 0.42589947f, 0.90451616f}; // j_mid_le_1
	joints[10].rotation={0.09986848f, 0.09054948f, -0.28658936f, 0.94852181f}; // j_thumb_le_1
	joints[11].rotation={0.00300895f, 0.03620722f, 0.35800130f, 0.93301396f}; // j_index_le_2
	joints[12].rotation={-0.02938024f, 0.00102170f, 0.48112764f, 0.87615749f}; // j_mid_le_2
	joints[15].rotation={-0.01981478f, -0.06927743f, -0.17161094f, 0.98252618f}; // j_thumb_le_2
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
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{{{6.54486791f, 3.39358195f, 9.21474620f}, {-0.11445629f, 0.24975577f, -0.02679784f, 0.96114707f}},{4.34685845f, 0.77232395f, 0.98769930f},bottom_pinch_fingers,magazine_grasp_kind::bottom_pinch},
	{{{6.35545677f, 5.79472733f, 2.93101996f}, {-0.61498757f, 0.27985831f, -0.26053052f, 0.68963285f}},{5.93930026f, 1.32591097f, 2.06814016f},body_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
