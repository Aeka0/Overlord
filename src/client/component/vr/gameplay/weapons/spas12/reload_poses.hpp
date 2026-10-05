#pragma once
#include "../../tube_profile.hpp"
#include "../hand_poses/shell_grasp.hpp"
namespace vr::gameplay::weapons::spas12
{
// h2_wpn_sho_spas12_reload_loop frame 6 SHA-256 b48a6dd6baaa2e3807b4311c4673b1d406a4344aae7d73659174c2154bf6c30a
// h2_wpn_sho_spas12_reload_empty_in frame 15 SHA-256 2daedcdca433ab414625a8ad9bcc38b82dd628354cb50ec0f1e997ef00194471
// h2_wpn_sho_spas12_reload_empty_in frame 18 SHA-256 2daedcdca433ab414625a8ad9bcc38b82dd628354cb50ec0f1e997ef00194471
// h2_wpn_sho_spas12_reload_loop frame 9 SHA-256 b48a6dd6baaa2e3807b4311c4673b1d406a4344aae7d73659174c2154bf6c30a
inline constexpr auto shell_in_wrist=hand_poses::shell_grasp::shell_in_wrist;
inline constexpr hands::anchor bolt_rest={{7.52907776f, 0.00000000f, 3.90408095f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor bolt_open={{3.77619380f, 0.00013245f, 3.90401513f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor pump_rest={{18.13734760f, 0.00000000f, 2.73268091f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor port_shell={{6.96410084f, -0.00029659f, 4.56682610f}, {0.00106815f, -0.00622582f, -0.00280772f, 0.99997611f}};
inline constexpr hands::anchor lifter_rest={{5.28827277f, 0.00000000f, 2.46044200f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor lifter_loaded={{5.28836350f, -0.00018363f, 2.58084084f}, {0.00000000f, -0.05496462f, 0.00000000f, 0.99848830f}};
inline constexpr hands::vec shell_center={-0.28554799f, -0.01565585f, -0.00735416f};
inline constexpr hands::vec rack_contact={4.76918552f, 0.63180669f, 0.82428276f};
inline constexpr hands::vec rack_low={12.27981387f, -1.23844898f, 1.67411012f};
inline constexpr hands::vec rack_high={24.15119832f, 1.21565194f, 4.73911180f};
inline constexpr hands::vec port_center={6.20951337f, -1.25984252f, 4.88480792f};
inline constexpr hands::vec tube_center={6.44852440f, 0.55990860f, 1.25984252f};
inline constexpr hands::vec port_forward={0.90793849f, 0.39161457f, -0.14928406f};
inline constexpr hands::vec tube_forward={0.90080456f, -0.39862922f, 0.17217982f};
// Native RIGHT loading hand retargeted to canonical left around the rigid shell.
inline constexpr auto& shell_fingers=hand_poses::shell_grasp::shell_fingers;
inline constexpr std::array<joint_pose, 18> rack_fingers{{
	{"j_index_le_0", {0.56999395f, -0.18817705f, -0.00823999f, 0.79976771f}},
	{"j_mid_le_0", {0.57899066f, -0.33054226f, 0.11221530f, 0.73683062f}},
	{"j_pinkypalm_le", {0.64940457f, -0.32877651f, -0.09356992f, 0.67928225f}},
	{"j_ringpalm_le", {0.70367056f, -0.17005041f, -0.05148511f, 0.68795341f}},
	{"j_thumb_le_0", {-0.09021137f, -0.17419706f, 0.21795995f, 0.95603910f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02710043f, -0.00051881f, -0.06436352f, 0.99755834f}},
	{"j_mid_le_1", {-0.03204474f, 0.00787385f, 0.47685629f, 0.87836166f}},
	{"j_pinky_le_0", {-0.01464878f, 0.06009051f, 0.67045628f, 0.73936657f}},
	{"j_ring_le_0", {-0.14242842f, -0.05911374f, 0.54447512f, 0.82447956f}},
	{"j_thumb_le_1", {0.10895169f, 0.07950116f, -0.18476009f, 0.97348488f}},
	{"j_index_le_2", {-0.00561541f, 0.03588980f, 0.12720125f, 0.99121150f}},
	{"j_mid_le_2", {-0.02877862f, -0.00579845f, 0.26657610f, 0.96336668f}},
	{"j_pinky_le_1", {0.01895184f, 0.02063035f, 0.28723791f, 0.95744953f}},
	{"j_ring_le_1", {0.01058985f, 0.00912497f, 0.52457907f, 0.85124696f}},
	{"j_thumb_le_2", {-0.03747647f, -0.06155541f, 0.09417948f, 0.99294344f}},
	{"j_pinky_le_2", {-0.02688689f, -0.02957252f, -0.01287885f, 0.99911796f}},
	{"j_ring_le_2", {0.00033570f, -0.00317393f, -0.04190198f, 0.99911663f}},
}};
inline constexpr hands::vec pump_symmetry_center{};
inline constexpr part_grip_pose rack_pose{"pump",wrists[0],rack_contact,rack_fingers,&pump_symmetry_center};
}
