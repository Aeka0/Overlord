#pragma once
#include "magazine_collision.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::famas
{
// Left magazine source: h2_wpn_asl_famas_reload frame 46: f87e72437c6937f075b9db658ad4edccdfac9a060c143f601dc1544a0d5aa49e
inline constexpr hands::anchor magazine_rest={{-4.67243082f, 0.00000000f, 2.35906605f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{4.63856299f, 0.00000000f, 4.14140318f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{5.42878980f, 3.93344351f, 2.25862311f}, {-0.58623720f, 0.20406605f, -0.08013219f, 0.77991142f}};
inline constexpr hands::anchor magazine_well={{-4.91765696f, -0.00059358f, 0.27559055f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::vec magazine_top={-0.24522614f, -0.00059358f, -0.14712356f};
inline constexpr hands::vec action_grab_low={4.10099518f, -0.18434399f, 3.75748694f};
inline constexpr hands::vec action_grab_high={5.46910594f, 0.18364999f, 5.07167981f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.67545824f, -0.23236520f, 0.14193992f, 0.68528508f}},
	{"j_mid_le_0", {0.53535714f, -0.32801383f, 0.32834953f, 0.70568140f}},
	{"j_pinkypalm_le", {0.74956415f, -0.24439191f, -0.04626600f, 0.61342125f}},
	{"j_ringpalm_le", {0.70340003f, -0.15833215f, -0.06335727f, 0.69003274f}},
	{"j_thumb_le_0", {-0.16096824f, -0.08135385f, 0.22085694f, 0.95848474f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02069155f, -0.01751763f, 0.57814268f, 0.81548515f}},
	{"j_mid_le_1", {-0.01913517f, -0.00964388f, 0.63814432f, 0.76961852f}},
	{"j_pinky_le_0", {-0.17029146f, 0.23984240f, 0.65052559f, 0.70020919f}},
	{"j_ring_le_0", {-0.19638517f, 0.09283663f, 0.64759803f, 0.73036362f}},
	{"j_thumb_le_1", {0.11722172f, 0.13299981f, -0.21253114f, 0.96093737f}},
	{"j_index_le_2", {0.01065099f, 0.03476069f, 0.54780900f, 0.83581311f}},
	{"j_mid_le_2", {-0.02914516f, 0.00375377f, 0.56050269f, 0.82763108f}},
	{"j_pinky_le_1", {0.02450631f, 0.01351967f, 0.57542519f, 0.81737538f}},
	{"j_ring_le_1", {0.01095606f, 0.00863667f, 0.56007132f, 0.82832692f}},
	{"j_thumb_le_2", {-0.02874830f, -0.06607226f, -0.04199327f, 0.99651621f}},
	{"j_pinky_le_2", {-0.03134311f, 0.01214660f, 0.48348352f, 0.87470781f}},
	{"j_ring_le_2", {-0.00122073f, -0.00289924f, 0.44962680f, 0.89321097f}},
}};
// h2_wpn_asl_famas_reload_empty frame 78: 8ab7e0fbc703808ae2c7a17846a7c75280e47ef60512880571b320fa804e61c0
inline constexpr std::array<joint_pose, 18> top_handle_fingers{{
	{"j_index_le_0", {0.49260268f, -0.44658045f, 0.12234955f, 0.73684401f}},
	{"j_mid_le_0", {0.47594165f, -0.31376442f, 0.09436432f, 0.81616592f}},
	{"j_pinkypalm_le", {0.67319845f, -0.15606949f, -0.26331004f, 0.67313742f}},
	{"j_ringpalm_le", {0.72094715f, -0.09564612f, -0.13745697f, 0.67245268f}},
	{"j_thumb_le_0", {0.06564554f, -0.04571689f, 0.26047639f, 0.96216042f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.01309253f, -0.02047806f, 0.26178959f, 0.96481887f}},
	{"j_mid_le_1", {-0.01489313f, -0.03225828f, 0.31406812f, 0.94873538f}},
	{"j_pinky_le_0", {-0.06448580f, 0.14059918f, 0.23279586f, 0.96014558f}},
	{"j_ring_le_0", {-0.14319135f, 0.00671400f, 0.09378240f, 0.98521877f}},
	{"j_thumb_le_1", {0.13007167f, 0.11298107f, -0.27100808f, 0.94703287f}},
	{"j_index_le_2", {-0.01898247f, 0.03991811f, 0.33838841f, 0.93996781f}},
	{"j_mid_le_2", {-0.02893180f, -0.00512715f, 0.28937900f, 0.95676353f}},
	{"j_pinky_le_1", {0.02795508f, 0.00213631f, 0.86428821f, 0.50221493f}},
	{"j_ring_le_1", {0.01364195f, 0.00262463f, 0.88025675f, 0.47429429f}},
	{"j_thumb_le_2", {-0.13220631f, -0.06469930f, -0.20389436f, 0.96786496f}},
	{"j_pinky_le_2", {-0.03213559f, 0.01022357f, 0.42676552f, 0.90373335f}},
	{"j_ring_le_2", {-0.00093081f, -0.00302131f, 0.36370185f, 0.93151005f}},
}};
inline constexpr std::array<part_grip_pose,1> action_grips{{
	{"top_handle",{{1.44216838f, 3.26162018f, 1.86074677f}, {0.75026201f, -0.30681237f, 0.21261310f, 0.54568192f}},{5.01629673f, -0.32737084f, 2.44493764f},top_handle_fingers},
}};
// Physical pull and directed spare-magazine strike share the seated magazine.
inline constexpr magazine_contact_profile contacts{{3.85221114f, 0.94156474f, 1.50234748f},{-5.99687381f, -0.36448698f, -2.48031496f},{-3.67908177f, 0.36339499f, 0.27559055f},magazine_latch,strike_regions};
}
