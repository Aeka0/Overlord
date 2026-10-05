#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::aug
{
	// The exposed release behind the magazine accepts a rearward/upward strike.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
		.05f,.05f,.10f,{0,0,-1},.025f,.06f,.15f,.012f,{-.70710678f,0,.70710678f},true};
	inline constexpr physical_reload::handle_catch manual_catch{};
	// Native rear stroke; the shared catch handles lift, roll and palm impact.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,0.120f,0.f,0.113f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::charging_handle,&manual_magazine,&manual_catch};
}
