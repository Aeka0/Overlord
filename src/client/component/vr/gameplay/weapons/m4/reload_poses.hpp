#pragma once
#include "../../families/ar.hpp"
#include "handle_poses.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::m4
{
// Source reload frame 31: b49948ddab9d4777fc62fba389bb684dcf7fe300bfb1133fdbd04caebcadee21.
// Source right-hand charging-handle pullout_first frame 16: 786f64bb5b06a599d9dc59e58d4c44bc17426f5c914fe984563f7a9a8d1d4fd9.
// Cm -> native units once. Left handle wrist uses weapon-Y reflection and
// hand-local -I parity; fingers retain parent-local source rotations. HMD fit pending.
inline constexpr hands::anchor magazine_rest={{5.01963285f, -0.00839800f, 2.57139994f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr auto handle_rest=families::ar::contact_reference;
inline constexpr hands::anchor rigid_in_magazine={{-1.15086274f, -0.03456803f, -1.06253801f}, {-0.00011400f, -0.20543804f, -0.00072973f, 0.97866985f}};
inline constexpr hands::anchor magazine_in_wrist={{7.02824843f, 2.03990392f, 6.56654910f}, {-0.11033794f, 0.25853619f, -0.05279427f, 0.95822615f}};
// Loaded native world magazine fit: p95 0.2125 cm, max 1.9847 cm; not identical mesh.
inline constexpr hands::vec magazine_top={-0.88844824f, 0.02040414f, 0.76019394f};
// Offline side-view receiver lip at about Z=0.5 cm, not the magazine bottom.
// Capture tolerance and full-magazine ejection clearance share this actual mouth.
inline constexpr hands::anchor magazine_well={{4.13118461f, 0.01200614f, 0.19685039f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
inline constexpr std::array<joint_pose, 15> magazine_fingers{{
	{"j_index_le_0", {0.68156935f, -0.04155346f, -0.12424076f, 0.71993109f}},
	{"j_mid_le_0", {0.53209117f, -0.33303440f, 0.33620042f, 0.70209427f}},
	{"j_thumb_le_0", {-0.17095562f, -0.05918042f, 0.23806576f, 0.95425183f}},
	{"j_index_le_1", {0.02141506f, -0.01663256f, 0.54504695f, 0.83796693f}},
	{"j_mid_le_1", {-0.01868920f, -0.01052984f, 0.60231719f, 0.79796857f}},
	{"j_pinky_le_0", {-0.19916203f, 0.27872308f, 0.42981888f, 0.83540628f}},
	{"j_ring_le_0", {-0.19594632f, 0.07977232f, 0.48598962f, 0.84797141f}},
	{"j_thumb_le_1", {0.10160708f, 0.08859415f, -0.26812623f, 0.95390534f}},
	{"j_index_le_2", {0.00214519f, 0.03626865f, 0.33566368f, 0.94128098f}},
	{"j_mid_le_2", {-0.02905368f, 0.00448623f, 0.58128717f, 0.81316725f}},
	{"j_pinky_le_1", {0.02688944f, 0.00791524f, 0.73945459f, 0.67262264f}},
	{"j_ring_le_1", {0.01272623f, 0.00579852f, 0.74062399f, 0.67177416f}},
	{"j_thumb_le_2", {-0.02021771f, -0.06916091f, -0.16588876f, 0.98350848f}},
	{"j_pinky_le_2", {-0.02819193f, 0.01843516f, 0.65351204f, 0.75616624f}},
	{"j_ring_le_2", {-0.00170906f, -0.00259411f, 0.60000199f, 0.79999248f}},
}};
inline const auto& handle_fingers=handle_pose_fingers_0;

inline constexpr auto handle_grab_low=families::ar::contact_low,handle_grab_high=families::ar::contact_high;
}
