#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::mp5
{
	// Preserve the downward pull and reuse the shared forward latch strike.
	inline constexpr auto manual_magazine=physical_reload::rocking_magazine();
	inline constexpr physical_reload::handle_catch manual_catch{};
	// Native rear stroke; the shared catch handles lift, roll and palm impact.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,0.065f,0.f,0.060f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::charging_handle,&manual_magazine,&manual_catch};
}
