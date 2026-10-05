#pragma once
#include "../../part_grip_pose.hpp"
namespace vr::gameplay::weapons::hand_poses::right_handle
{
// M14 EBR reload_empty frame 73, actual right-hand charging grasp.
// Source SHA-256 caa768d5965af08cd21bd674493b27de3e915adf70a82ab63f3545dee2b24c37
// Canonical left hand reflected about the real contact with native glove axes.
inline constexpr hands::quat rotation={0.94447560f, -0.21312708f, 0.24814352f, -0.03110435f};
inline constexpr hands::vec contact={4.37358360f, 0.79543320f, 1.05518495f};
inline constexpr std::array<joint_pose, 18> fingers{{
	{"j_index_le_0", {0.62861298f, -0.29321844f, 0.15124797f, 0.70426750f}},
	{"j_mid_le_0", {0.57416778f, -0.35559702f, 0.18881554f, 0.71290309f}},
	{"j_pinkypalm_le", {0.68287513f, -0.24475590f, -0.07525785f, 0.68418738f}},
	{"j_ringpalm_le", {0.70476075f, -0.15460609f, -0.03720190f, 0.69139371f}},
	{"j_thumb_le_0", {-0.03347836f, -0.36167053f, 0.19290520f, 0.91151588f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987477f}},
	{"j_index_le_1", {0.01635789f, -0.02154611f, 0.74233276f, 0.66948507f}},
	{"j_mid_le_1", {-0.01892155f, -0.01010166f, 0.61958877f, 0.78463347f}},
	{"j_pinky_le_0", {-0.12888150f, 0.05423213f, 0.60042957f, 0.78735809f}},
	{"j_ring_le_0", {-0.16654040f, -0.06308213f, 0.60216397f, 0.77825670f}},
	{"j_thumb_le_1", {0.09878680f, 0.10586711f, -0.40039659f, 0.90482921f}},
	{"j_index_le_2", {0.01550310f, 0.03286791f, 0.66184444f, 0.74875984f}},
	{"j_mid_le_2", {-0.02932855f, 0.00213632f, 0.51625557f, 0.85592959f}},
	{"j_pinky_le_1", {0.02414027f, 0.01422171f, 0.55351716f, 0.83236635f}},
	{"j_ring_le_1", {0.01196312f, 0.00717174f, 0.66148160f, 0.74983168f}},
	{"j_thumb_le_2", {0.00283821f, -0.06198312f, -0.31894912f, 0.94573860f}},
	{"j_pinky_le_2", {-0.03109870f, 0.01306207f, 0.50588044f, 0.86194387f}},
	{"j_ring_le_2", {-0.00134281f, -0.00277716f, 0.49118888f, 0.87104763f}},
}};
}
