#pragma once
#include "../hand_poses/magazine.hpp"
namespace vr::gameplay::weapons::m16
{
// Offline mesh-fitted at native hand scale. Source hand recipes are shared;
// wrist placement and joint corrections belong to this magazine only. HMD fit pending.
// Receiver h2_viewmodel_m16_base; SHA-256 58be751a7b9f437110a5ac2176cd92fe12702cb195af7b8a5d463de700a5cfeb.
inline constexpr auto bottom_pinch_fingers=[] {
	auto joints=hand_poses::magazine::bottom_pinch;
	joints[0].rotation={0.69475332f, -0.01087473f, -0.08605259f, 0.71399896f}; // j_index_le_0
	joints[1].rotation={0.51525293f, -0.33115768f, 0.31187537f, 0.72634892f}; // j_mid_le_0
	joints[4].rotation={-0.10441296f, -0.07904606f, 0.30024474f, 0.94482948f}; // j_thumb_le_0
	joints[6].rotation={0.02173456f, -0.01621283f, 0.52864299f, 0.84841107f}; // j_index_le_1
	joints[7].rotation={-0.01867101f, -0.01056207f, 0.60093957f, 0.79900655f}; // j_mid_le_1
	joints[10].rotation={0.10549183f, 0.08393090f, -0.22491808f, 0.96500722f}; // j_thumb_le_1
	joints[11].rotation={0.00186926f, 0.03628392f, 0.32849435f, 0.94380689f}; // j_index_le_2
	joints[12].rotation={-0.02922177f, 0.00321411f, 0.54524851f, 0.83775881f}; // j_mid_le_2
	joints[15].rotation={-0.02040317f, -0.06910642f, -0.16324971f, 0.98394997f}; // j_thumb_le_2
	return joints;
}();
inline constexpr auto body_wrap_fingers=[] {
	auto joints=hand_poses::magazine::body_wrap;
	joints[0].rotation={0.52890905f, -0.21738228f, 0.25158564f, 0.78083598f}; // j_index_le_0
	joints[1].rotation={0.50692929f, -0.27210047f, 0.20772224f, 0.79109766f}; // j_mid_le_0
	joints[4].rotation={-0.22553070f, -0.33996074f, 0.36834537f, 0.83539469f}; // j_thumb_le_0
	joints[6].rotation={0.02666984f, -0.00486057f, 0.09666578f, 0.99494765f}; // j_index_le_1
	joints[7].rotation={-0.01414509f, -0.01610005f, 0.30199987f, 0.95306704f}; // j_mid_le_1
	joints[8].rotation={-0.16439943f, 0.06491207f, 0.29496456f, 0.93901819f}; // j_pinky_le_0
	joints[9].rotation={-0.20373768f, -0.04505105f, 0.32649496f, 0.92187982f}; // j_ring_le_0
	joints[10].rotation={0.10766582f, 0.08114328f, -0.19961602f, 0.97055514f}; // j_thumb_le_1
	joints[11].rotation={-0.00598659f, 0.03587862f, 0.11705565f, 0.99245900f}; // j_index_le_2
	joints[12].rotation={-0.02877878f, -0.00591902f, 0.26231556f, 0.96453475f}; // j_mid_le_2
	joints[13].rotation={0.02031172f, 0.01929495f, 0.35243569f, 0.93541661f}; // j_pinky_le_1
	joints[14].rotation={0.00881167f, 0.01084011f, 0.36864552f, 0.92946508f}; // j_ring_le_1
	joints[15].rotation={-0.03552558f, -0.06268237f, 0.06312823f, 0.99540127f}; // j_thumb_le_2
	joints[16].rotation={-0.03309238f, 0.00629359f, 0.31632353f, 0.94805312f}; // j_pinky_le_2
	joints[17].rotation={-0.00068005f, -0.00305121f, 0.28399939f, 0.95881936f}; // j_ring_le_2
	return joints;
}();
inline constexpr std::array<magazine_grasp_pose,2> magazine_grasps{{
	{{{7.02964428f, 2.53717902f, 6.55119386f}, {-0.11941553f, 0.25760089f, -0.03324799f, 0.95826734f}},{4.37549331f, 0.69451249f, 0.97235780f},bottom_pinch_fingers,magazine_grasp_kind::bottom_pinch},
	{{{6.34435486f, 4.17581204f, 1.14143957f}, {-0.67363346f, 0.42880904f, -0.14418535f, 0.58442395f}},{5.18881474f, 1.61483150f, 1.17722786f},body_wrap_fingers,magazine_grasp_kind::body_wrap},
}};
}
