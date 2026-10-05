#pragma once
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::m4
{
// Offline mesh-fitted at native hand scale. Source hand recipes are shared;
// wrist placement and joint corrections belong to this magazine only. HMD fit pending.
// Receiver h2_viewmodel_m4_base; SHA-256 90307e116e650ae82a652c24bac94b8ef9fcec4af19f9b097e82a3c56fd3ff44.
inline constexpr auto bottom_pinch_fingers=[] {
	auto joints=hand_poses::magazine::bottom_pinch;
	joints[0].rotation={0.66332751f, -0.00234962f, -0.07733170f, 0.74431908f}; // j_index_le_0
	joints[1].rotation={0.44509890f, -0.37916998f, 0.35772982f, 0.72811158f}; // j_mid_le_0
	joints[4].rotation={-0.13223366f, -0.06101835f, 0.29512793f, 0.94429367f}; // j_thumb_le_0
	joints[6].rotation={0.02228536f, -0.01544700f, 0.49881179f, 0.86628607f}; // j_index_le_1
	joints[7].rotation={-0.01756114f, -0.01231949f, 0.52078951f, 0.85341556f}; // j_mid_le_1
	joints[10].rotation={0.10542431f, 0.08401570f, -0.22569392f, 0.96482606f}; // j_thumb_le_1
	joints[11].rotation={0.00625920f, 0.03578881f, 0.44061516f, 0.89696057f}; // j_index_le_2
	joints[12].rotation={-0.02893906f, 0.00517431f, 0.60041715f, 0.79914642f}; // j_mid_le_2
	joints[15].rotation={-0.01658688f, -0.07012034f, -0.21690544f, 0.97352968f}; // j_thumb_le_2
	return joints;
}();
inline constexpr auto body_wrap_fingers=[] {
	auto joints=hand_poses::magazine::body_wrap;
	joints[0].rotation={0.52735252f, -0.21964798f, 0.25533913f, 0.78003591f}; // j_index_le_0
	joints[1].rotation={0.50563914f, -0.27449048f, 0.21145443f, 0.79010826f}; // j_mid_le_0
	joints[4].rotation={-0.22601212f, -0.33964087f, 0.36952857f, 0.83487199f}; // j_thumb_le_0
	joints[6].rotation={0.02608164f, -0.00739281f, 0.19152289f, 0.98111369f}; // j_index_le_1
	joints[7].rotation={-0.01562220f, -0.01467113f, 0.39190130f, 0.91975762f}; // j_mid_le_1
	joints[8].rotation={-0.16255783f, 0.06541156f, 0.29915521f, 0.93797785f}; // j_pinky_le_0
	joints[9].rotation={-0.20262936f, -0.04438640f, 0.33085182f, 0.92060212f}; // j_ring_le_0
	joints[10].rotation={0.10771753f, 0.08107462f, -0.19899720f, 0.97068221f}; // j_thumb_le_1
	joints[11].rotation={-0.00373915f, 0.03618194f, 0.17867402f, 0.98323572f}; // j_index_le_2
	joints[12].rotation={-0.02909169f, -0.00411414f, 0.32191150f, 0.94631376f}; // j_mid_le_2
	joints[13].rotation={0.02206650f, 0.01726066f, 0.44041456f, 0.89735732f}; // j_pinky_le_1
	joints[14].rotation={0.00980948f, 0.00994624f, 0.45597978f, 0.88988049f}; // j_ring_le_1
	joints[15].rotation={-0.03555222f, -0.06266727f, 0.06355131f, 0.99537434f}; // j_thumb_le_2
	joints[16].rotation={-0.03263587f, 0.00834354f, 0.37478743f, 0.92649861f}; // j_pinky_le_2
	joints[17].rotation={-0.00086886f, -0.00300290f, 0.34319702f, 0.93925824f}; // j_ring_le_2
	return joints;
}();
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{{{7.15318048f, 2.69893237f, 6.41431133f}, {-0.12578558f, 0.27362344f, -0.04479221f, 0.95252394f}},{4.50924834f, 1.05826339f, 0.77410926f},bottom_pinch_fingers,magazine_grasp_kind::bottom_pinch},
	{{{6.26661172f, 4.26482317f, 0.94178036f}, {-0.67860001f, 0.45103046f, -0.11208297f, 0.56878023f}},{4.68067601f, 1.33678192f, 1.11861118f},body_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
