#pragma once
#include "magazine_collision.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::fn2000
{
// h2_wpn_asl_fn2000_reload frame 15: fb979e3a69829c2bc6384134b9baed07b7f0b2cbca286c1dca57495e9a7dd2a1
// h2_wpn_asl_fn2000_reload_empty frame 87: acd1934965a0d6808db0a243ce0211a8aca105942effa6b0966dcd7409cfb9e3
// Handle contact retains the real receiver side; native le hand retargeted to left.
inline constexpr hands::anchor magazine_rest={{-4.49311114f, 0.00000000f, 1.15001699f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_rest={{6.64973297f, 0.00000000f, 3.75094677f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor magazine_in_wrist={{4.83093232f, 3.43702081f, -0.50910393f}, {-0.84043820f, 0.30319089f, -0.03980972f, 0.44738585f}};
inline constexpr hands::anchor magazine_well={{-5.39432022f, 0.00350286f, 0.00000000f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr hands::anchor action_wrist={{-1.25708664f, 3.78892159f, 4.09640432f}, {0.96757828f, -0.14696595f, 0.11387028f, -0.17095861f}};
inline constexpr hands::vec magazine_top={-0.90120908f, 0.00350286f, 1.47798259f};
inline constexpr hands::vec action_grab_low={3.86365680f, 1.37976399f, 3.57089568f};
inline constexpr hands::vec action_grab_high={5.92290773f, 1.60688997f, 4.33286382f};
inline constexpr hands::vec action_contact={7.43263991f, 0.40920763f, 1.29751090f};
inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	{"j_index_le_0", {0.54897354f, -0.22571761f, 0.01580878f, 0.80463016f}},
	{"j_mid_le_0", {0.56255332f, -0.24265510f, 0.12369276f, 0.78061025f}},
	{"j_pinkypalm_le", {0.66743844f, -0.20349701f, -0.13422013f, 0.70363332f}},
	{"j_ringpalm_le", {0.69100385f, -0.10376350f, -0.05688681f, 0.71309937f}},
	{"j_thumb_le_0", {-0.12045712f, -0.20584832f, 0.31040522f, 0.92019843f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.02148477f, -0.01959264f, 0.59055648f, 0.80647230f}},
	{"j_mid_le_1", {-0.01977590f, -0.00839255f, 0.68583931f, 0.72743585f}},
	{"j_pinky_le_0", {-0.18857381f, 0.21057765f, 0.61534452f, 0.73583156f}},
	{"j_ring_le_0", {-0.17300931f, 0.04419077f, 0.47547806f, 0.86141487f}},
	{"j_thumb_le_1", {0.13104711f, 0.04620524f, -0.28864545f, 0.94729907f}},
	{"j_index_le_2", {0.01617483f, 0.03262433f, 0.67638268f, 0.73564971f}},
	{"j_mid_le_2", {-0.02935856f, 0.00119021f, 0.48759021f, 0.87257804f}},
	{"j_pinky_le_1", {0.02624597f, 0.00976594f, 0.69106241f, 0.72225239f}},
	{"j_ring_le_1", {0.03808710f, 0.02615436f, 0.70534383f, 0.70735805f}},
	{"j_thumb_le_2", {0.00149539f, -0.07202274f, -0.45389582f, 0.88813798f}},
	{"j_pinky_le_2", {-0.02691761f, 0.02026451f, 0.70272673f, 0.71066162f}},
	{"j_ring_le_2", {0.07486126f, -0.03369214f, 0.56428236f, 0.82149014f}},
}};
inline constexpr std::array<joint_pose, 18> action_fingers{{
	{"j_index_le_0", {0.68477292f, -0.09897132f, -0.03039637f, 0.72136453f}},
	{"j_mid_le_0", {0.63447974f, -0.17712941f, 0.05621509f, 0.75026695f}},
	{"j_pinkypalm_le", {0.58522321f, -0.28830280f, 0.03839587f, 0.75691549f}},
	{"j_ringpalm_le", {0.65798190f, -0.14441039f, 0.04223780f, 0.73784919f}},
	{"j_thumb_le_0", {-0.01312313f, -0.34226949f, 0.23734550f, 0.90903602f}},
	{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	{"j_index_le_1", {0.03158660f, -0.01825004f, 0.26972212f, 0.96224695f}},
	{"j_mid_le_1", {-0.01208522f, -0.01779213f, 0.18188867f, 0.98308387f}},
	{"j_pinky_le_0", {-0.05197270f, 0.06146389f, 0.77064859f, 0.63215646f}},
	{"j_ring_le_0", {-0.21024360f, -0.02777205f, 0.44749628f, 0.86877697f}},
	{"j_thumb_le_1", {0.09094478f, 0.06064002f, -0.26828709f, 0.95711748f}},
	{"j_index_le_2", {-0.01089510f, 0.03466899f, -0.01974545f, 0.99914437f}},
	{"j_mid_le_2", {-0.02691774f, -0.01181085f, 0.05484261f, 0.99806224f}},
	{"j_pinky_le_1", {0.02380442f, 0.01477095f, 0.53297481f, 0.84566720f}},
	{"j_ring_le_1", {0.00982704f, 0.00988808f, 0.45903286f, 0.88830991f}},
	{"j_thumb_le_2", {-0.04304136f, -0.05777166f, 0.18725177f, 0.97966660f}},
	{"j_pinky_le_2", {-0.03363144f, 0.00189215f, 0.18875720f, 0.98144591f}},
	{"j_ring_le_2", {-0.00045777f, -0.00314339f, 0.20776865f, 0.97817284f}},
}};
inline constexpr std::array<part_grip_pose,1> action_grips{{{"charging_handle",action_wrist,action_contact,action_fingers}}};
inline constexpr magazine_contact_profile contacts{{2.75814350f, 1.96885495f, 1.77360288f},{-6.19465009f, -0.39393700f, -3.78186008f},{-3.11638183f, 0.47244096f, 2.69195883f},magazine_latch,strike_regions};
inline constexpr float action_stroke_m=0.13723570f;
}
