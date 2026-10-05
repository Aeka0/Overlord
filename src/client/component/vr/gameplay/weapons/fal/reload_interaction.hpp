#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::fal
{
	// Authored hand pull is forward/downward; native reload instead strikes and
	// throws the old magazine. Reuse the shared swept spare-magazine latch rule.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
		.05f,.05f,.10f,{.35f,0,-.93674970f},.025f,.06f,.15f,.012f,{1,0,0}};
	// Native first_pullout j_bolt handle stroke: 143.65 mm, non-reciprocating.
	// The internal bolt surface shares this bone; its reviewed mesh partition
	// now presents fire/empty lock independently of the non-reciprocating handle.
	inline constexpr physical_reload::profile reload_interaction{
		.18f,part_grip_capture::radius_m,.144f,0.f,.135f,.18f,.045f,.06f,.08715574f,.25f,
		{-1,0,0},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::charging_handle,&manual_magazine};
}
