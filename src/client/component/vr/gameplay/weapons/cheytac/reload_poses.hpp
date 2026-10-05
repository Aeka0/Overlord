#pragma once
#include "handle_poses.hpp"
#include "magazine_collision.hpp"
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::cheytac
{
	// h2_wpn_sni_cheytac_reload frame 18: 77a53e82b124e981019a7d34eb5961324c7fb2dd9ffe83d4c9ddad0e3abce269
	// h2_wpn_sni_cheytac_rechamber frame 11: 06b76517a3123fb7401732165cad36e71514943ece6ced70c970cca65ce1873d
	// Handle contact retains the real receiver side; native ri hand retargeted to left.
	inline constexpr hands::anchor magazine_rest = {
	    {7.15173887f, 0.00000000f, 2.10291401f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor action_rest = {
	    {4.29616500f, 0.00269100f, 4.01477701f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor magazine_in_wrist = {
	    {5.54412171f, 4.11898094f, 1.71585136f}, {-0.60454433f, 0.31821342f, -0.18255575f, 0.70706419f}};
	inline constexpr hands::anchor magazine_well = {
	    {5.97531941f, -0.00746667f, 0.59055118f}, {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::vec magazine_top = {-1.17641946f, -0.00746667f, 1.51215232f};
	inline constexpr hands::vec action_grab_low = {0.41543597f, -4.58125392f, 0.43322796f};
	inline constexpr hands::vec action_grab_high = {1.50285396f, -2.16536597f, 2.51165187f};
	inline constexpr std::array<joint_pose, 18> magazine_fingers{{
	    {"j_index_le_0", {0.78539441f, -0.18988630f, -0.07443470f, 0.58442988f}},
	    {"j_mid_le_0", {0.61962023f, -0.22507507f, -0.11417028f, 0.74322078f}},
	    {"j_pinkypalm_le", {0.68992458f, -0.23196871f, -0.19034191f, 0.65875986f}},
	    {"j_ringpalm_le", {0.71477335f, -0.11462742f, -0.10507513f, 0.68184956f}},
	    {"j_thumb_le_0", {-0.13049847f, -0.33283825f, 0.55236012f, 0.75305188f}},
	    {"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
	    {"j_index_le_1", {0.02447578f, -0.01165804f, 0.35225382f, 0.93551177f}},
	    {"j_mid_le_1", {-0.01877888f, -0.01046775f, 0.60412855f, 0.79659681f}},
	    {"j_pinky_le_0", {-0.32288871f, 0.20493362f, 0.32880935f, 0.86349841f}},
	    {"j_ring_le_0", {-0.23004669f, -0.07165689f, 0.11444350f, 0.96376683f}},
	    {"j_thumb_le_1", {0.09866769f, 0.09152626f, -0.18732516f, 0.97303490f}},
	    {"j_index_le_2", {0.01218552f, 0.03425024f, 0.58440341f, 0.81064856f}},
	    {"j_mid_le_2", {-0.02890148f, 0.00525944f, 0.60342138f, 0.79688122f}},
	    {"j_pinky_le_1", {0.02302096f, 0.01600176f, 0.48957436f, 0.87151072f}},
	    {"j_ring_le_1", {0.01153601f, 0.00782292f, 0.61949602f, 0.78487604f}},
	    {"j_thumb_le_2", {-0.03897250f, -0.05844350f, -0.11066116f, 0.99137259f}},
	    {"j_pinky_le_2", {-0.03286821f, 0.00732439f, 0.34695238f, 0.93727802f}},
	    {"j_ring_le_2", {-0.00168869f, -0.00262458f, 0.59769310f, 0.80171892f}},
	}};
	inline const auto& action_fingers=handle_pose_fingers_0;

	inline constexpr magazine_contact_profile contacts{{4.01121080f, -0.48158370f, 1.36480399f},
	    {4.94795784f, -0.58579999f, -2.66842692f}, {10.11595839f, 0.58579999f, 3.65687280f},
	    {4.56824941f, -0.00000049f, -0.49618799f},strike_regions};
	inline constexpr float action_stroke_m = 0.23027761f;
} // namespace vr::gameplay::weapons::cheytac
