#pragma once
#include "../../physical_reload_profile.hpp"
namespace vr::gameplay::weapons::p90
{
// h2_wpn_smg_p90_reload frame 13 SHA256 67c1a330d6a61cc8bef0bd8b6aed94b6382504ffed612be0bb7f7907f93f68e7
// h2_wpn_smg_p90_reload_empty frame 93 SHA256 92ba9c1fc38a9eee6f7b02ff1dfac687ac0245db8f62d890808ce870da570f97
inline constexpr hands::anchor magazine_rest={{5.89681798f, 0.00000000f, 3.24834613f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{5.11877293f, 0.98491400f, 2.59839005f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{-3.04654673f, -5.42551009f, 4.14853204f}, {-0.41064699f, 0.86908410f, -0.27324917f, 0.03737325f}};
inline constexpr hands::anchor magazine_well={{-4.79581351f, 0.01435869f, 2.91490925f}, {1.00000000f, 0.00000000f, 0.00000000f, 0.00000000f}};
inline constexpr hands::anchor action_wrist={{1.68497228f, 4.73623843f, 1.50376213f}, {0.93217671f, -0.27470180f, 0.20936398f, -0.10840771f}};
inline constexpr hands::vec magazine_top={-10.69263149f, 0.01435869f, -0.33343688f};
inline constexpr hands::vec action_grab_low={1.34031500f, -1.18959795f, 2.09894199f};
inline constexpr hands::vec action_grab_high={6.06782493f, 1.18959701f, 2.93522501f};
inline constexpr hands::vec action_contact={5.44771698f, 0.98140729f, 0.88977294f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.48573245f, -0.38696773f, 0.26669717f, 0.73701600f}},
	{"j_mid_le_0", {0.38172544f, -0.45301666f, 0.32886926f, 0.73546353f}},
	{"j_pinkypalm_le", {0.68994477f, -0.23194498f, -0.19031696f, 0.65875427f}},
	{"j_ringpalm_le", {0.71478111f, -0.11456762f, -0.10507627f, 0.68185131f}},
	{"j_thumb_le_0", {-0.29801091f, -0.19989467f, 0.44462067f, 0.82069731f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02633754f, -0.00646994f, 0.15608495f, 0.98737124f}},
	{"j_mid_le_1", {-0.01217521f, -0.01770091f, 0.18747527f, 0.98203435f}},
	{"j_pinky_le_0", {-0.18143337f, 0.09873142f, 0.62059851f, 0.75643343f}},
	{"j_ring_le_0", {-0.23194114f, 0.01605289f, 0.52571518f, 0.81827206f}},
	{"j_thumb_le_1", {0.08429337f, 0.10513782f, -0.43285041f, 0.89133674f}},
	{"j_index_le_2", {-0.00103942f, 0.03634748f, 0.25049370f, 0.96743511f}},
	{"j_mid_le_2", {-0.02939101f, -0.00066781f, 0.43302732f, 0.90090125f}},
	{"j_pinky_le_1", {0.01840069f, 0.02108809f, 0.26261102f, 0.96449581f}},
	{"j_ring_le_1", {0.00958114f, 0.01016100f, 0.43300382f, 0.90128389f}},
	{"j_thumb_le_2", {-0.04690669f, -0.05462783f, 0.25360740f, 0.96462368f}},
	{"j_pinky_le_2", {-0.03317586f, 0.00576449f, 0.30094530f, 0.95304672f}},
	{"j_ring_le_2", {-0.00076295f, -0.00305182f, 0.30115325f, 0.95357057f}},
}};
inline constexpr std::array<joint_pose, 18> action_fingers{{
	{"j_index_le_0", {0.54829121f, -0.23508222f, 0.03561482f, 0.80177595f}},
	{"j_mid_le_0", {0.45533459f, -0.47407289f, 0.12308072f, 0.74348937f}},
	{"j_pinkypalm_le", {0.68994477f, -0.23194498f, -0.19031696f, 0.65875427f}},
	{"j_ringpalm_le", {0.71478111f, -0.11456762f, -0.10507627f, 0.68185131f}},
	{"j_thumb_le_0", {0.53861405f, -0.40851536f, 0.50666159f, 0.53507395f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02438465f, -0.01174980f, 0.35640039f, 0.93394116f}},
	{"j_mid_le_1", {-0.01718207f, -0.01278737f, 0.49516707f, 0.86853372f}},
	{"j_pinky_le_0", {-0.16165420f, -0.03476008f, 0.69468276f, 0.70005394f}},
	{"j_ring_le_0", {-0.22357825f, -0.20722035f, 0.56108191f, 0.76958403f}},
	{"j_thumb_le_1", {0.11313175f, 0.12216520f, -0.31397342f, 0.93471791f}},
	{"j_index_le_2", {0.00863690f, 0.03524953f, 0.49828494f, 0.86625342f}},
	{"j_mid_le_2", {-0.03170883f, -0.00048830f, 0.47328253f, 0.88033968f}},
	{"j_pinky_le_1", {0.02655094f, 0.00878928f, 0.71681443f, 0.69670285f}},
	{"j_ring_le_1", {0.01159702f, 0.00775169f, 0.62407212f, 0.78124222f}},
	{"j_thumb_le_2", {-0.06582789f, -0.01065087f, -0.04367163f, 0.99681796f}},
	{"j_pinky_le_2", {-0.03320400f, 0.00570694f, 0.30100277f, 0.95302794f}},
	{"j_ring_le_2", {-0.00079347f, -0.00302130f, 0.30115327f, 0.95357063f}},
}};
// Both exterior lobes are skinned to j_bolt; reflect contact to the actual opposite knob.
inline constexpr hands::vec symmetry_center{0,0,0};
inline constexpr std::array<part_grip_pose,1> action_grips{{{"bilateral_handle",action_wrist,action_contact,action_fingers,&symmetry_center}}};
inline constexpr magazine_contact_profile contacts{{3.50698891f, 3.13445225f, 1.94247112f},{-5.65681420f, -1.08456302f, 3.05474086f},{5.76072603f, 1.08456396f, 4.39124370f},{-5.55546829f, 0.00000055f, 3.64427208f},{}};
inline constexpr float action_stroke_m=0.07012703f;
}
