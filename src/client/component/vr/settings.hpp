#pragma once
#include "debug_options.hpp"
#include "controller_pose_pipeline.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace vr::settings
{
	struct number_setting
	{
		const char* name;
		float default_value, min, max, step;
		float display_scale{1.f}; // Native units per launcher unit.
	};

	inline constexpr const char* turn_mode = "vr_turnMode";
	inline constexpr number_setting scripted_head_gain{"vr_scriptedHeadScale", .1f, 0.f, .25f, .01f};
	inline constexpr const char* disable_lens_flare = "vr_disableLensFlare";
	inline constexpr const char* camera_bob = "vr_cameraBob";
	inline constexpr const char* recoil = "vr_recoil";
	inline constexpr const char* recoil_penalty = "vr_recoilPenalty";
	struct choice_setting
	{
		const char* name;
		std::array<const char*, 5> values; // Null terminated for native enum registration.
		int default_index{};
		std::array<const char*, 5> label_keys{}; // Launcher localization keys, paired with values.
	};
	inline constexpr choice_setting killfeed_style{
		.name = "vr_killfeedStyle",
		.values = {"off", "bocw", "mw_classic", "mw2019"},
		.default_index = 3,
		.label_keys = {"choice.disabled", "choice.bocw", "choice.mwClassic", "choice.mw2019"}};
	inline constexpr choice_setting cheat_health{
	    .name = "vr_cheatHealth",
	    .values = {"off", "demigod", "god"},
	    .label_keys = {"choice.disabled", "choice.demigod", "choice.god"}};
	inline constexpr choice_setting cheat_notarget{.name = "vr_cheatNotarget",
	                                               .values = {"off", "on"},
	                                               .label_keys = {"choice.disabled", "choice.enabled"}};
	inline constexpr choice_setting cheat_ammo{
	    .name = "vr_cheatAmmo",
	    .values = {"off", "reserve", "infinite"},
	    .label_keys = {"choice.disabled", "choice.infiniteReserve", "choice.infiniteAmmo"}};
	inline constexpr choice_setting runtime_backend{
		.name = "vr_runtimeBackend",
		.values = {"openxr", "openvr"},
		.label_keys = {"choice.openxr", "choice.openvr"}};
	inline constexpr choice_setting controller_pose_mode{
		.name = "vr_controllerPoseMode",
		.values = {"standard", "legacy"},
		.default_index = 1,
		.label_keys = {"choice.standardControllerPose", "choice.legacyControllerPose"}};
	inline constexpr std::array choices{
		runtime_backend, controller_pose_mode, killfeed_style,
	    choice_setting{
	        .name = turn_mode, .values = {"smooth", "snap"}, .label_keys = {"choice.smooth", "choice.snap"}},
	    choice_setting{.name = recoil_penalty,
	                   .values = {"all", "long", "off"},
	                   .default_index = 1,
	                   .label_keys = {"choice.allWeapons", "choice.longWeapons", "choice.off"}},
	    cheat_health,
	    cheat_notarget,
	    cheat_ammo};
	static_assert(
	    []
	    {
		    for (const auto& field : choices)
		    {
			    if (field.default_index < 0 || field.default_index >= int(field.values.size()) ||
			        !field.values[field.default_index])
				    return false;
			    bool terminated = false;
			    for (std::size_t i = 0; i < field.values.size(); ++i)
			    {
				    if (!field.values[i])
				    {
					    terminated = true;
					    continue;
				    }
				    if (terminated || !field.label_keys[i] || !*field.label_keys[i])
					    return false;
			    }
			    if (!terminated)
				    return false;
		    }
		    return true;
	    }(),
	    "Every choice must be null-terminated with a valid default and launcher label");
	inline constexpr std::array stabilization_toggles{
	    "vr_desktopStabilization", "vr_headStabilization", "vr_handStabilization"};
	inline constexpr std::array stabilization_strengths{
	    number_setting{"vr_desktopStabilizationStrength", 50, 0, 100, 1},
	    number_setting{"vr_headStabilizationStrength", 30, 0, 100, 1},
	    number_setting{"vr_handStabilizationStrength", 40, 0, 100, 1}};
	struct boolean_setting
	{
		const char* name;
		bool default_value;
	};
	inline constexpr boolean_setting recording_mode{"vr_recordingMode", false};
	inline constexpr number_setting recording_dim{"vr_recordingDim", 65.f, 0.f, 100.f, 1.f};
	inline constexpr boolean_setting quick_reload{"vr_quickReload", true};
	inline constexpr boolean_setting chambering_guide{"vr_chamberingGuide", false};
	inline constexpr boolean_setting physical_ladders{"vr_physicalLadders", true};
	inline constexpr boolean_setting smart_ammo_selection{"vr_smartAmmoSelection", true};
	inline constexpr boolean_setting discard_ammo_penalty{"vr_discardAmmoPenalty", false};
	inline constexpr boolean_setting hide_hud{"vr_hideHud", false};
	inline constexpr boolean_setting disable_blur{"vr_disableBlur", false};
	inline constexpr boolean_setting disable_dog_pounce{"vr_disableDogPounce", true};
	inline constexpr boolean_setting ads_comfort{"vr_adsComfort", true};
	inline constexpr auto toggles = []
	{
		constexpr std::array gameplay{recording_mode,
		                              quick_reload,
		                              chambering_guide,
		                              physical_ladders,
		                              smart_ammo_selection,
		                              discard_ammo_penalty,
		                              hide_hud,
		                              disable_blur,
		                              disable_dog_pounce,
		                              ads_comfort,
		                              boolean_setting{disable_lens_flare, false},
		                              boolean_setting{camera_bob, true},
		                              boolean_setting{recoil, true},
		                              boolean_setting{stabilization_toggles[0], false},
		                              boolean_setting{stabilization_toggles[1], false},
		                              boolean_setting{stabilization_toggles[2], false}};
		std::array<boolean_setting, gameplay.size() + debug_options::names.size()> result{};
		std::copy(gameplay.begin(), gameplay.end(), result.begin());
		for (std::size_t i{}; i < debug_options::names.size(); ++i)
			result[gameplay.size() + i] = {debug_options::names[i], false};
		return result;
	}();
	inline constexpr number_setting turn_speed{"vr_turnSpeed", 90.f, 15.f, 360.f, 5.f};
	inline constexpr number_setting snap_angle{"vr_snapAngle", 30.f, 5.f, 90.f, 5.f};
	inline constexpr number_setting aim_assist_strength{"vr_aimAssistStrength", 0.f, 0.f, 100.f, 1.f};
	inline constexpr number_setting enemy_melee_damage{"vr_enemyMeleeDamageScale", .5f, .1f, 2.f, .1f};
	inline constexpr number_setting grenade_throw_speed{
	    "vr_grenadeThrowSpeedScale", 2.5f, .25f, 10.f, .25f, 2.5f};
	// Advanced runtime tuning only; launcher saves/reset preserve this value.
	inline constexpr number_setting football_throw_speed{"vr_footballThrowSpeedScale", 1.5f, 1.f, 3.f, .1f};
	inline constexpr float max_aim_assist_degrees = 10.f;
	inline float aim_assist_degrees(float strength) noexcept
	{
		return std::isfinite(strength)
		           ? std::clamp(strength, aim_assist_strength.min, aim_assist_strength.max) *
		                 (max_aim_assist_degrees / aim_assist_strength.max)
		           : 0.f;
	}
	inline constexpr float max_hand_offset = .5f;
	// One grip-local point determines both wrist placement and its rotation
	// centre. Position and angle calibration remain independent. Retain the
	// existing Legacy defaults for previously accepted controller setups.
	inline constexpr number_setting hand_inward{
	    "vr_handOffsetInward", 0.f, -max_hand_offset, max_hand_offset, .01f};
	inline constexpr number_setting hand_back{
	    "vr_handOffsetBack", 0.f, -max_hand_offset, max_hand_offset, .01f};
	inline constexpr number_setting hand_up{"vr_handOffsetUp", 0.f, -max_hand_offset, max_hand_offset, .01f};
	inline constexpr number_setting hand_pitch{"vr_handAnglePitch", 0.f, -180.f, 180.f, 1.f};
	inline constexpr number_setting hand_yaw{"vr_handAngleYaw", 0.f, -180.f, 180.f, 1.f};
	inline constexpr number_setting hand_roll{"vr_handAngleRoll", 0.f, -180.f, 180.f, 1.f};
	inline constexpr std::array hand_angles{hand_pitch, hand_yaw, hand_roll};
	inline constexpr std::array hand_alignment{
	    hand_inward, hand_back, hand_up, hand_pitch, hand_yaw, hand_roll};
	struct alignment_preset
	{
		const char* id;
		const char* label;
		std::array<float, hand_alignment.size()> values;
	};
	inline constexpr std::array alignment_presets{
	    alignment_preset{"none", "None", {0, 0, 0, 0, 0, 0}},
	    alignment_preset{"meta_quest_3", "Meta Quest 3", {-.02f, .12f, -.1f, -20.f, 0, 0}}};
	// Application calibration in OpenXR grip coordinates, independent of the
	// retained legacy settings. The Standard trial baseline uses the former
	// physical wrist's legacy back/up point (0.12/0 m), expressed through the
	// existing canonical Touch grip relation, rather than the legacy device origin.
	// Lateral placement and aim angles retain their accepted defaults. This is
	// an application baseline candidate, not anatomy measured for every controller.
	// See docs/vr-runtime-rendering.md for provenance and removal boundaries.
	inline constexpr std::array standard_hand_alignment{
		number_setting{"vr_standardHandOffsetInward", -.007f, -max_hand_offset, max_hand_offset, .01f},
		number_setting{"vr_standardHandOffsetBack", .016253897f, -max_hand_offset, max_hand_offset, .01f},
		number_setting{"vr_standardHandOffsetUp", .008063812f, -max_hand_offset, max_hand_offset, .01f},
		number_setting{"vr_standardHandAnglePitch", 0.f, -180.f, 180.f, 1.f},
		number_setting{"vr_standardHandAngleYaw", 0.f, -180.f, 180.f, 1.f},
		number_setting{"vr_standardHandAngleRoll", 0.f, -180.f, 180.f, 1.f}};
	inline constexpr std::array standard_alignment_presets{
		alignment_preset{"standard_default", "Standard wrist baseline", {-.007f, .016253897f, .008063812f, 0, 0, 0}},
		alignment_preset{"meta_quest_3", "Meta Quest 3", {-.027f, .051438062f, -.085542142f, -20, 0, 0}}};
	inline const auto& active_hand_alignment() noexcept
	{
		return controller_pose_pipeline::selected() == controller_pose_pipeline::mode::standard
			? standard_hand_alignment : hand_alignment;
	}
	inline constexpr std::array numbers{turn_speed,
										standard_hand_alignment[0], standard_hand_alignment[1],
										standard_hand_alignment[2], standard_hand_alignment[3],
										standard_hand_alignment[4], standard_hand_alignment[5],
	                                    snap_angle,
	                                    aim_assist_strength,
	                                    enemy_melee_damage,
	                                    grenade_throw_speed,
	                                    recording_dim,
	                                    hand_inward,
	                                    hand_back,
	                                    hand_up,
	                                    hand_pitch,
	                                    hand_yaw,
	                                    hand_roll,
	                                    stabilization_strengths[0],
	                                    stabilization_strengths[1],
	                                    stabilization_strengths[2]};
}
