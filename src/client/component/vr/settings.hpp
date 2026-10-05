#pragma once
#include "debug_options.hpp"
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
	inline constexpr number_setting scripted_head_gain{"vr_scriptedHeadScale",.1f,0.f,.25f,.01f};
	inline constexpr const char* disable_lens_flare = "vr_disableLensFlare";
	inline constexpr const char* camera_bob = "vr_cameraBob";
	inline constexpr const char* recoil = "vr_recoil";
	inline constexpr const char* recoil_penalty = "vr_recoilPenalty";
	struct choice_setting
	{
		const char* name;
		std::array<const char*,4> values; // Null terminated for native enum registration.
		int default_index{};
	};
	inline constexpr choice_setting cheat_health{"vr_cheatHealth",{"off","demigod","god"}};
	inline constexpr choice_setting cheat_notarget{"vr_cheatNotarget",{"off","on"}};
	inline constexpr choice_setting cheat_ammo{"vr_cheatAmmo",{"off","reserve","infinite"}};
	inline constexpr std::array choices{
		choice_setting{turn_mode,{"smooth","snap"}},
		choice_setting{recoil_penalty,{"all","long","off"},1},
		cheat_health,cheat_notarget,cheat_ammo};
	inline constexpr std::array stabilization_toggles{"vr_desktopStabilization","vr_headStabilization","vr_handStabilization"};
	inline constexpr std::array stabilization_strengths{
		number_setting{"vr_desktopStabilizationStrength",50,0,100,1},
		number_setting{"vr_headStabilizationStrength",30,0,100,1},
		number_setting{"vr_handStabilizationStrength",40,0,100,1}};
	struct boolean_setting {const char* name;bool default_value;};
	inline constexpr boolean_setting recording_mode{"vr_recordingMode",false};
	inline constexpr number_setting recording_dim{"vr_recordingDim",65.f,0.f,100.f,1.f};
	inline constexpr boolean_setting quick_reload{"vr_quickReload",true};
	inline constexpr boolean_setting chambering_guide{"vr_chamberingGuide",false};
	inline constexpr boolean_setting physical_ladders{"vr_physicalLadders",true};
	inline constexpr boolean_setting discard_ammo_penalty{"vr_discardAmmoPenalty",false};
	inline constexpr boolean_setting hide_hud{"vr_hideHud",false};
	inline constexpr boolean_setting disable_blur{"vr_disableBlur",false};
	inline constexpr boolean_setting disable_dog_pounce{"vr_disableDogPounce",true};
	inline constexpr auto toggles=[] {
		constexpr std::array gameplay{
		recording_mode,
		quick_reload,
		chambering_guide,
		physical_ladders,
		discard_ammo_penalty,
		hide_hud, disable_blur, disable_dog_pounce,
		boolean_setting{disable_lens_flare,false}, boolean_setting{camera_bob,true}, boolean_setting{recoil,true},
		boolean_setting{stabilization_toggles[0],false},boolean_setting{stabilization_toggles[1],false},boolean_setting{stabilization_toggles[2],false}};
		std::array<boolean_setting,gameplay.size()+debug_options::names.size()> result{};
		std::copy(gameplay.begin(),gameplay.end(),result.begin());
		for(std::size_t i{};i<debug_options::names.size();++i)
			result[gameplay.size()+i]={debug_options::names[i],false};
		return result;
	}();
	inline constexpr number_setting turn_speed{"vr_turnSpeed", 90.f, 15.f, 360.f, 5.f};
	inline constexpr number_setting snap_angle{"vr_snapAngle", 30.f, 5.f, 90.f, 5.f};
	inline constexpr number_setting aim_assist_strength{"vr_aimAssistStrength", 0.f, 0.f, 100.f, 1.f};
	inline constexpr number_setting enemy_melee_damage{"vr_enemyMeleeDamageScale",.5f,.1f,2.f,.1f};
	inline constexpr number_setting grenade_throw_speed{"vr_grenadeThrowSpeedScale",2.5f,.25f,10.f,.25f,2.5f};
	// Advanced runtime tuning only; launcher saves/reset preserve this value.
	inline constexpr number_setting football_throw_speed{"vr_footballThrowSpeedScale",1.5f,1.f,3.f,.1f};
	inline constexpr float max_aim_assist_degrees = 10.f;
	inline float aim_assist_degrees(float strength) noexcept
	{
		return std::isfinite(strength)
			? std::clamp(strength, aim_assist_strength.min, aim_assist_strength.max) *
				(max_aim_assist_degrees / aim_assist_strength.max)
			: 0.f;
	}
	inline constexpr float max_hand_offset = .5f;
	// Physical grip-to-wrist baseline, separate from presentation alignment.
	// Advanced saved console controls; launcher edits preserve these entries.
	inline constexpr number_setting wrist_inward{"vr_wristPivotInward", -.02f, -max_hand_offset, max_hand_offset, .01f};
	inline constexpr number_setting wrist_back{"vr_wristPivotBack", .12f, -max_hand_offset, max_hand_offset, .01f};
	// Zero Up matches the saved rigid-local trial that improved alignment before
	// the pivot/alignment split. The old -5 cm default was not that trial's value.
	// This remains a tuning baseline, not a measured anatomical calibration.
	inline constexpr number_setting wrist_up{"vr_wristPivotUp", 0.f, -max_hand_offset, max_hand_offset, .01f};
	inline constexpr std::array wrist_pivots{wrist_inward,wrist_back,wrist_up};
	inline constexpr number_setting hand_inward{"vr_handOffsetInward", 0.f, -max_hand_offset, max_hand_offset, .01f};
	inline constexpr number_setting hand_back{"vr_handOffsetBack", 0.f, -max_hand_offset, max_hand_offset, .01f};
	inline constexpr number_setting hand_up{"vr_handOffsetUp", 0.f, -max_hand_offset, max_hand_offset, .01f};
	inline constexpr number_setting hand_pitch{"vr_handAnglePitch", 0.f, -180.f, 180.f, 1.f};
	inline constexpr number_setting hand_yaw{"vr_handAngleYaw", 0.f, -180.f, 180.f, 1.f};
	inline constexpr number_setting hand_roll{"vr_handAngleRoll", 0.f, -180.f, 180.f, 1.f};
	inline constexpr std::array hand_angles{hand_pitch,hand_yaw,hand_roll};
	inline constexpr std::array hand_alignment{hand_inward,hand_back,hand_up,hand_pitch,hand_yaw,hand_roll};
	struct alignment_preset
	{
		const char* id;
		const char* label;
		std::array<float,hand_alignment.size()> values;
	};
	inline constexpr std::array alignment_presets{
		alignment_preset{"none","None",{0,0,0,0,0,0}},
		alignment_preset{"meta_quest_3","Meta Quest 3",{-.02f,.12f,-.1f,-20.f,0,0}}};
	inline constexpr std::array numbers{turn_speed, snap_angle, aim_assist_strength, enemy_melee_damage, grenade_throw_speed, recording_dim, hand_inward, hand_back, hand_up,
		hand_pitch,hand_yaw,hand_roll,stabilization_strengths[0],stabilization_strengths[1],stabilization_strengths[2]};
}
