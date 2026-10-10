#pragma once
#include "weapon_interaction.hpp"
#include <string_view>
#include <string>

namespace vr::gameplay::weapons::javelin_screen
{
	// Screen input/camera ownership is separate from ammunition and native locks.
	bool enabled() noexcept;
	std::string format_status();
	hold capture_owner() noexcept;
	void input(const controller_input::frame&,const hold&,bool allowed) noexcept;
	bool requested(const hold&) noexcept;
	bool allows_fire(const hold&) noexcept;
	bool lock_aim(const hold&,muzzle_frame&) noexcept;
	std::uint64_t camera_epoch() noexcept;
	void apply_camera(float* origin,float (*axis)[3],bool allowed,float* tan_half) noexcept;
	inline bool overlay_material(std::string_view name) noexcept
	{
		// Reuse native 2D ink and native status/target decisions. Flat lens shadow,
		// eyepiece, scene-distortion and grain shaders belong to the desktop view,
		// not the independent optical display.
		constexpr std::string_view names[]{"hud_javelin_bg_fixed","hud_javelin_bg",
			"h1_hud_javelin_active_area","hud_javelin_lock_box","hud_javelin_lock_box_corners",
			"hud_javelin_day_on","hud_javelin_night_on","hud_javelin_lock_on",
			"hud_javelin_top_on","hud_javelin_dir_on","hud_javelin_rocket_on",
			"hud_javelin_norocket_on","hud_javelin_clu_on",
			"h1_hud_javelin_target_corner","javelin_hud_target","javelin_hud_target_offscreen"};
		for(auto value:names)if(value==name)return true;
		return false;
	}
}
