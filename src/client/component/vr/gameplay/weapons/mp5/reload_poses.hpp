#pragma once
#include "handle_poses.hpp"
#include "magazine_collision.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::mp5
{
// h2_wpn_smg_mp5k_reload frame 22: 7f1595e55380720a99bbdd3a6168d7e2777edfff5fdfdeb79d52e04341a0d321
// h2_wpn_smg_mp5k_reload_empty frame 11: 3c4c3e1bd72f0932df4fb98c2f90b8f2e461615c9465ab35df456202aa37d06c
// Native left grasp moved with the action pivot to its closed rest.
// Closest reference hand/tab skin: 0.646 mm. HMD fit pending.
inline constexpr hands::anchor magazine_rest={{6.58167291f, 0.00000000f, 2.83803790f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{11.08938878f, 0.00000000f, 5.82553796f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{5.41858936f, 2.92770434f, 0.75649017f}, {-0.67370432f, 0.37734202f, -0.30157723f, 0.55927334f}};
inline constexpr hands::anchor magazine_well={{6.06613413f, 0.00000004f, 1.77165354f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={-0.51553878f, 0.00000004f, 1.01793049f};
inline constexpr hands::vec action_grab_low={10.35702473f, 0.77873999f, 5.76750688f};
inline constexpr hands::vec action_grab_high={10.82820592f, 1.16881892f, 6.18998723f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.60813728f, -0.22589613f, 0.11050660f, 0.75294640f}},
	{"j_mid_le_0", {0.49656218f, -0.45008291f, 0.26199904f, 0.69441189f}},
	{"j_pinkypalm_le", {0.67253818f, -0.25290756f, -0.18540044f, 0.67034084f}},
	{"j_ringpalm_le", {0.71056513f, -0.13095542f, -0.09567589f, 0.68468533f}},
	{"j_thumb_le_0", {-0.09500272f, -0.16925315f, 0.39765030f, 0.89677315f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02584946f, -0.01107834f, 0.65090585f, 0.75863736f}},
	{"j_mid_le_1", {-0.01803656f, -0.01165815f, 0.55104590f, 0.83419853f}},
	{"j_pinky_le_0", {0.00738550f, 0.10937864f, 0.44755517f, 0.88751121f}},
	{"j_ring_le_0", {-0.04986728f, -0.02020327f, 0.53706509f, 0.84182313f}},
	{"j_thumb_le_1", {0.07431227f, 0.12170733f, -0.24960989f, 0.95778908f}},
	{"j_index_le_2", {0.00714140f, 0.03350965f, 0.36268556f, 0.93128153f}},
	{"j_mid_le_2", {-0.02935848f, -0.00119021f, 0.41526072f, 0.90922780f}},
	{"j_pinky_le_1", {0.02319424f, 0.01455744f, 0.52934747f, 0.84796307f}},
	{"j_ring_le_1", {0.01089516f, 0.00869782f, 0.55409683f, 0.83233548f}},
	{"j_thumb_le_2", {0.12994797f, 0.01825009f, 0.10147416f, 0.98614576f}},
	{"j_pinky_le_2", {-0.03454679f, 0.00527968f, 0.28809337f, 0.95696440f}},
	{"j_ring_le_2", {-0.01135287f, 0.00210577f, 0.32764634f, 0.94472988f}},
}};
inline const auto& action_fingers=handle_pose_fingers_0;

// Magazine body pull contact and receiver mouth are authored against mesh.
// Latch is the lower j_clip_release paddle behind the well, export cm / 2.54.
inline constexpr magazine_contact_profile contacts{{3.89214852f, 1.65350226f, 1.13411936f},{5.81103232f, -0.41381998f, -3.84376436f},{8.56174065f, 0.41381998f, 3.90985694f},{5.54147274f, 0.00000000f, 1.45612894f},strike_regions};
// Native reload_empty frame 19 raised handle orientation.
inline constexpr charging_handle_catch handle_catch{{0.26035271f, 0.00000000f, 0.00000000f, 0.96551358f},{-0.54716801f, 1.04566897f, 0.35496096f},{0.00000000f, 0.00000000f, 0.00000000f}};
}
