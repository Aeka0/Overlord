#pragma once
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::tavor
{
// Offline mesh-fitted at native hand scale. Source hand recipes are shared;
// wrist placement and joint corrections belong to this magazine only. HMD fit pending.
// Receiver h2_viewmodel_tavor_base; SHA-256 0c1603120beb517c31ec33de82a6d6315ae827963c16ad276151acc0c6e604a2.
inline constexpr auto bottom_pinch_fingers=[] {
	auto joints=hand_poses::magazine::bottom_pinch;
	joints[0].rotation={0.61173247f, -0.14106743f, -0.14632619f, 0.76450770f}; // j_index_le_0
	joints[1].rotation={0.47702782f, -0.39621693f, 0.39398186f, 0.67840615f}; // j_mid_le_0
	joints[4].rotation={-0.06148357f, -0.03986540f, 0.26758640f, 0.96074348f}; // j_thumb_le_0
	joints[6].rotation={0.02243690f, -0.01522605f, 0.49022791f, 0.87117240f}; // j_index_le_1
	joints[7].rotation={-0.01689482f, -0.01321850f, 0.47557737f, 0.87941231f}; // j_mid_le_1
	joints[10].rotation={0.09917110f, 0.09131274f, -0.29385544f, 0.94629597f}; // j_thumb_le_1
	joints[11].rotation={0.00277936f, 0.03622557f, 0.35207952f, 0.93526467f}; // j_index_le_2
	joints[12].rotation={-0.02873246f, 0.00622000f, 0.62899289f, 0.77685501f}; // j_mid_le_2
	joints[15].rotation={-0.01682274f, -0.07006413f, -0.21362832f, 0.97425405f}; // j_thumb_le_2
	return joints;
}();
inline constexpr auto body_wrap_fingers=[] {
	auto joints=hand_poses::magazine::body_wrap;
	joints[0].rotation={0.53244093f, -0.21215788f, 0.24293974f, 0.78260844f}; // j_index_le_0
	joints[1].rotation={0.50985095f, -0.26658577f, 0.19912894f, 0.79330429f}; // j_mid_le_0
	joints[4].rotation={-0.22320443f, -0.34149259f, 0.36263328f, 0.83789003f}; // j_thumb_le_0
	joints[6].rotation={0.02589295f, -0.00802876f, 0.21547120f, 0.97613383f}; // j_index_le_1
	joints[7].rotation={-0.01597649f, -0.01428450f, 0.41428837f, 0.90989332f}; // j_mid_le_1
	joints[8].rotation={-0.16861715f, 0.06375887f, 0.28531047f, 0.94132938f}; // j_pinky_le_0
	joints[9].rotation={-0.20626714f, -0.04657448f, 0.31645630f, 0.92473785f}; // j_ring_le_0
	joints[10].rotation={0.10741602f, 0.08147368f, -0.20259693f, 0.96993728f}; // j_thumb_le_1
	joints[11].rotation={-0.00316320f, 0.03623684f, 0.19428974f, 0.98026955f}; // j_index_le_2
	joints[12].rotation={-0.02915345f, -0.00365092f, 0.33692185f, 0.94107407f}; // j_mid_le_2
	joints[13].rotation={0.02248222f, 0.01671557f, 0.46223902f, 0.88631272f}; // j_pinky_le_1
	joints[14].rotation={0.01004991f, 0.00970324f, 0.47761664f, 0.87845728f}; // j_ring_le_1
	joints[15].rotation={-0.03539712f, -0.06275501f, 0.06108931f, 0.99552848f}; // j_thumb_le_2
	joints[16].rotation={-0.03249904f, 0.00886156f, 0.38947593f, 0.92042044f}; // j_pinky_le_2
	joints[17].rotation={-0.00091651f, -0.00298870f, 0.35809246f, 0.93368090f}; // j_ring_le_2
	return joints;
}();
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{{{5.84012465f, 2.88855661f, 7.76183716f}, {-0.13619385f, 0.24867196f, -0.04337372f, 0.95798341f}},{4.30306674f, 0.69637784f, 1.02363234f},bottom_pinch_fingers,magazine_grasp_kind::bottom_pinch},
	{{{5.92655661f, 5.03158369f, 1.70270737f}, {-0.63645973f, 0.44938194f, -0.13859775f, 0.61136368f}},{5.00970532f, 1.37845324f, 1.29100683f},body_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
