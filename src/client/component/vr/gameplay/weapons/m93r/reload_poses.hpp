#pragma once
#include "../../part_grip_pose.hpp"

namespace vr::gameplay::weapons::m93r
{
// h2_wpn_pst_beretta393_reload frame 24: 45338daf18830fec931528dd866483540d983b142e67c8c6578c46676c4de8bc
// h2_wpn_pst_m9_pullout_first frame 13: 2ee0713a75fcdfe5594f57b22f1dc5017077b051a1df6449ca0ac9ab1c41c7ed
// M93R native reload releases the lock; reuse the M9 manual grasp and fit
// its finger skin to this receiver. No M93R manual-pull clip is implied.
// Rearward fit 6.697078 cm; nearest action contact 0.206 mm. HMD pending.
inline constexpr hands::anchor magazine_rest={{0.38369899f, 0.10194499f, 0.46101499f}, {0.00000000f, 0.14346763f, 0.00000000f, 0.98965501f}};
inline constexpr hands::anchor action_rest={{0.65721898f, 0.09766200f, 3.49900891f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{5.74515589f, -0.00017273f, 4.33920755f}, {0.01896801f, 0.27312608f, -0.14699687f, 0.95049160f}};
inline constexpr hands::anchor action_wrist={{-5.45426252f, 0.52433805f, 3.39821188f}, {0.99370920f, 0.05250228f, 0.07731445f, -0.06170910f}};
inline constexpr hands::anchor magazine_well={{-0.24466517f, 0.02137972f, -1.98075305f}, {0.00000000f, 0.14346763f, 0.00000000f, 0.98965501f}};
// Well is at the grip mouth, above the protruding magazine baseplate.
inline constexpr hands::vec magazine_top={0.09088438f, -0.08056528f, 0.39828225f};
inline constexpr hands::vec action_grab_low={-0.83464567f, -0.76377953f, 2.67716535f}, action_grab_high={1.14173228f, 0.89763780f, 3.88582677f};
inline constexpr hands::vec action_contact={4.86028435f, 1.22018262f, 0.75077273f};
inline constexpr std::array<joint_pose,15> magazine_fingers{{
	{"j_index_le_0", {0.63231680f, -0.40394542f, 0.18866656f, 0.63356807f}},
	{"j_mid_le_0", {0.57146364f, -0.43414146f, 0.22413887f, 0.65932713f}},
	{"j_thumb_le_0", {0.10371514f, -0.31673469f, 0.09701414f, 0.93782225f}},
	{"j_index_le_1", {0.02697824f, -0.00253303f, 0.00823996f, 0.99959885f}},
	{"j_mid_le_1", {-0.01663264f, -0.01360214f, 0.45620120f, 0.88961722f}},
	{"j_pinky_le_0", {-0.36420671f, 0.23593646f, 0.46902319f, 0.76922344f}},
	{"j_ring_le_0", {-0.24221338f, -0.13572375f, 0.44585996f, 0.85095278f}},
	{"j_thumb_le_1", {0.10683523f, 0.08221695f, -0.20962168f, 0.96844481f}},
	{"j_index_le_2", {-0.00923690f, 0.03518772f, 0.02521498f, 0.99901987f}},
	{"j_mid_le_2", {-0.02936794f, -0.00115970f, 0.41710533f, 0.90838281f}},
	{"j_pinky_le_1", {0.02649927f, 0.00895109f, 0.71215070f, 0.70146920f}},
	{"j_ring_le_1", {0.01223798f, 0.00663170f, 0.69309060f, 0.72071608f}},
	{"j_thumb_le_2", {-0.02439715f, -0.06773306f, -0.10576315f, 0.99178181f}},
	{"j_pinky_le_2", {-0.03271583f, 0.00799585f, 0.36518432f, 0.93032583f}},
	{"j_ring_le_2", {-0.00133366f, -0.00285959f, 0.48418670f, 0.87495902f}},
}};
inline constexpr std::array<joint_pose,15> action_fingers{{
	{"j_index_le_0", {0.56339732f, -0.29437899f, 0.06472920f, 0.76924288f}},
	{"j_mid_le_0", {0.53905177f, -0.27851364f, 0.14566575f, 0.78143127f}},
	{"j_thumb_le_0", {-0.06137312f, -0.11203417f, 0.26487184f, 0.95578481f}},
	{"j_index_le_1", {0.02471975f, -0.01123070f, 0.33380824f, 0.94224990f}},
	{"j_mid_le_1", {-0.05499404f, -0.03509609f, 0.52064280f, 0.85127845f}},
	{"j_pinky_le_0", {-0.08517731f, 0.26642705f, 0.45277339f, 0.84661544f}},
	{"j_ring_le_0", {-0.15265552f, 0.15561585f, 0.35011278f, 0.91098905f}},
	{"j_thumb_le_1", {0.10318305f, 0.09588913f, -0.27167569f, 0.95202461f}},
	{"j_index_le_2", {-0.00155644f, 0.03634734f, 0.23691383f, 0.97084926f}},
	{"j_mid_le_2", {-0.02920618f, -0.00262459f, 0.37186756f, 0.92782252f}},
	{"j_pinky_le_1", {0.02200386f, 0.01736504f, 0.43614027f, 0.89944202f}},
	{"j_ring_le_1", {0.00994895f, 0.00973532f, 0.46729545f, 0.88399163f}},
	{"j_thumb_le_2", {-0.04129182f, -0.05899268f, 0.15762917f, 0.98486948f}},
	{"j_pinky_le_2", {-0.03164798f, 0.01159714f, 0.46718151f, 0.88351873f}},
	{"j_ring_le_2", {-0.00097659f, -0.00299082f, 0.37534761f, 0.92687878f}},
}};
inline constexpr std::array<part_grip_pose,1> action_grips{{{"rear_overhand",action_wrist,action_contact,action_fingers}}};
}
