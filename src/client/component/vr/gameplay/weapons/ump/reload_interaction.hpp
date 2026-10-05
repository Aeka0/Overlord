#pragma once
#include "../../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::ump
{
	// Pull the magazine or push its rear paddle forward with a spare magazine.
	// This remains independent of the receiver bolt catch.
	inline constexpr physical_reload::magazine_manipulation manual_magazine{
		.05f,.05f,.10f,{.206228228f,0,-.978504f},.025f,.06f,.15f,.012f,{1,0,0},true};
	inline constexpr physical_reload::handle_catch manual_catch{};
	// Native rear stroke; the shared catch handles lift, roll and palm impact.
	// Mesh-reviewed left receiver paddle; gun-local native units.
	// Receiver SHA256 f65f1857e9e152f906e2de28306b4d96ab9b3e64f8c5aa482845fe6ba4dc3cd4
	inline constexpr physical_reload::receiver_bolt_release bolt_release{{5.12596979f, 1.00368496f, 3.51649983f}};
	inline constexpr physical_reload::profile reload_interaction=[] {
		physical_reload::profile p{
		.18f,part_grip_capture::radius_m,0.085f,0.f,0.079f,
		.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,2,
		physical_reload::action_motion::charging_handle,&manual_magazine,&manual_catch};
		p.receiver_release=&bolt_release;return p;
	}();
}
