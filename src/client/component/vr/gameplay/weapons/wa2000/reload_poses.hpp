#pragma once
#include "../../physical_reload_profile.hpp"

namespace vr::gameplay::weapons::wa2000
{
	// h2_wpn_sni_wa2000_reload frame 20: 8afe88c1826ee0c6d6948dcdd283ca337eb0f204712cc2e8e8df4d4008c156b3
	// h2_wpn_sni_wa2000_reload_empty frame 96: 9eece0650a85ebf0f420273bc6e5ee01ccf7bda68380a7161e2c6a3a8ec4a261
	// Handle contact retains the real receiver side; native le hand retargeted to left.
	inline constexpr hands::anchor magazine_rest = {{-5.77110039f, 0.00000000f, 3.76670041f},
													{0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor action_rest = {{9.74647717f, 0.00000000f, 3.98558294f},
												  {0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor magazine_in_wrist = {{6.24686091f, 5.57528629f, 0.23610544f},
														{-0.72789958f, 0.25282624f, -0.11077197f, 0.62767082f}};
	inline constexpr hands::anchor magazine_well = {{-6.40862277f, -0.00199906f, 0.78740157f},
													{0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	inline constexpr hands::anchor action_wrist = {{5.48775162f, 4.93887542f, 4.00515426f},
												   {0.95260829f, -0.30029368f, 0.02660641f, -0.04066016f}};
	inline constexpr hands::vec magazine_top = {-0.63752238f, -0.00199906f, 0.09935574f};
	inline constexpr hands::vec action_grab_low = {9.49784752f, -2.33695601f, 3.83359766f};
	inline constexpr hands::vec action_grab_high = {10.40079838f, 2.33295696f, 4.30923597f};
	inline constexpr hands::vec action_contact = {5.76844530f, 0.12855192f, 0.31256278f};
	inline constexpr std::array<joint_pose, 18> magazine_fingers{{
		{"j_index_le_0", {0.72786436f, -0.13537972f, -0.00869775f, 0.67216824f}},
		{"j_mid_le_0", {0.42536375f, -0.24811360f, 0.29523382f, 0.81874435f}},
		{"j_pinkypalm_le", {0.49617388f, -0.27479085f, -0.06808734f, 0.82077133f}},
		{"j_ringpalm_le", {0.61610706f, -0.16092394f, -0.03561507f, 0.77022538f}},
		{"j_thumb_le_0", {-0.04647932f, -0.16354739f, 0.53992916f, 0.82435940f}},
		{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
		{"j_index_le_1", {-0.11777222f, -0.08291970f, 0.58297095f, 0.79962422f}},
		{"j_mid_le_1", {-0.01513726f, -0.08945021f, 0.66314010f, 0.74297694f}},
		{"j_pinky_le_0", {-0.19251109f, 0.26368037f, 0.52269141f, 0.78753148f}},
		{"j_ring_le_0", {-0.32483508f, 0.09561333f, 0.41696934f, 0.84349086f}},
		{"j_thumb_le_1", {0.13593147f, 0.05078356f, -0.39430504f, 0.90745093f}},
		{"j_index_le_2", {-0.00308237f, 0.03622550f, 0.19605104f, 0.97991949f}},
		{"j_mid_le_2", {0.03695741f, -0.00283818f, 0.47184017f, 0.88090462f}},
		{"j_pinky_le_1", {0.02563535f, 0.01116969f, 0.65046651f, 0.75902002f}},
		{"j_ring_le_1", {0.01249733f, 0.00627155f, 0.71506372f, 0.69891943f}},
		{"j_thumb_le_2", {-0.04767041f, -0.05401832f, 0.26719233f, 0.96094631f}},
		{"j_pinky_le_2", {-0.02371270f, 0.02389581f, 0.79704568f, 0.60298000f}},
		{"j_ring_le_2", {-0.00225838f, -0.00213631f, 0.76968145f, 0.63842055f}},
	}};
	inline constexpr std::array<joint_pose, 18> action_fingers{{
		{"j_index_le_0", {0.75561029f, -0.06888050f, -0.19867616f, 0.62035180f}},
		{"j_mid_le_0", {0.52402314f, -0.42358867f, 0.31119119f, 0.67017343f}},
		{"j_pinkypalm_le", {0.65501284f, -0.26624107f, -0.16360825f, 0.68797254f}},
		{"j_ringpalm_le", {0.68480095f, -0.16080111f, -0.05917505f, 0.70830006f}},
		{"j_thumb_le_0", {-0.12991730f, -0.22125920f, 0.18160040f, 0.94930878f}},
		{"j_webbing_le", {-0.67247924f, -0.03613412f, -0.16806640f, 0.71987476f}},
		{"j_index_le_1", {-0.04583917f, 0.04767030f, 0.53716065f, 0.84088332f}},
		{"j_mid_le_1", {-0.02011187f, -0.00747710f, 0.71865609f, 0.69503456f}},
		{"j_pinky_le_0", {-0.08835108f, 0.07470931f, 0.52720722f, 0.84182252f}},
		{"j_ring_le_0", {-0.12555178f, -0.05221660f, 0.46241141f, 0.87617685f}},
		{"j_thumb_le_1", {0.09639260f, 0.09425631f, -0.32210724f, 0.93705450f}},
		{"j_index_le_2", {0.04666263f, 0.04562501f, 0.65413136f, 0.75356030f}},
		{"j_mid_le_2", {-0.02496430f, 0.01547298f, 0.85415754f, 0.51918423f}},
		{"j_pinky_le_1", {0.02627620f, 0.00964376f, 0.69291644f, 0.72047440f}},
		{"j_ring_le_1", {0.01232946f, 0.00650043f, 0.70546465f, 0.70860805f}},
		{"j_thumb_le_2", {-0.01498439f, -0.07046632f, -0.23926201f, 0.96827871f}},
		{"j_pinky_le_2", {-0.02356005f, 0.02398731f, 0.79985154f, 0.59925541f}},
		{"j_ring_le_2", {-0.00241099f, -0.00201425f, 0.81769225f, 0.57564704f}},
	}};
	// Both knobs belong to j_reload. Left hand uses the native left knob;
	// right hand reflects the complete grasp onto the real symmetric right knob.
	// Keep one style per hand so acquisition and the latched visual pose agree.
	inline constexpr hands::vec action_symmetry_center{0,-.00199952f,0};
	inline constexpr std::array<part_grip_pose, 1> action_grips{{
		{"bilateral_charging_handle", action_wrist, action_contact, action_fingers, &action_symmetry_center}}};
	inline constexpr magazine_contact_profile contacts{{5.14932875f, 0.33272137f, 0.46131078f},
													   {-7.52271175f, -0.60623903f, -1.39289381f},
													   {-3.77520723f, 0.60224200f, 3.92109728f},
													   {-7.52271175f, 0.00000000f, 0.78740157f},{}};
	inline constexpr float action_stroke_m = 0.16439747f;
	inline constexpr hands::anchor bolt_rest = {{-5.77009494f, 0.00000000f, 4.96179813f},
												{0.00000000f, 0.00000000f, 0.00000000f, 1.00000000f}};
	// Native reload_empty frames 90, 92, 93, 98: handle take-up precedes bolt motion.
	inline constexpr std::array<bolt_travel_sample, 4> bolt_samples{
		{{0, 0}, {.082197f, 0}, {.138715f, .055966f}, {action_stroke_m, .11194f}}};
	inline constexpr charging_handle_bolt bolt{"j_bolt", bolt_rest, bolt_samples, .11194f};
} // namespace vr::gameplay::weapons::wa2000
