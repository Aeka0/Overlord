#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::acr
{
	// The side handle travels 112.03mm in native reload_empty and returns
	// forward independently of the follower lock, with no shot reciprocation.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.113f,0.f,.102f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::charging_handle};
}
