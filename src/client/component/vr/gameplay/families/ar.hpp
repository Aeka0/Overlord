#pragma once
#include "../physical_reload_gesture.hpp"

namespace vr::gameplay::weapons::families::ar
{
	// Shared, explicitly selected M4/M16 policy. This is not native admission.
	inline constexpr mechanics::rules reload_rules{30,mechanics::magazine_release::button,true,true,true};
	constexpr physical_reload::profile charging_handle(float stroke,float full_stroke,
		const physical_reload::receiver_bolt_release* release) noexcept
	{
		physical_reload::profile p{
			.18f,part_grip_capture::radius_m,stroke,0.f,full_stroke,
			.18f,.045f,.06f,.08715574f,.25f,{-1,0,0},.06f,.35f,.04f,.003f,1,
			physical_reload::action_motion::charging_handle};
		p.receiver_release=release;
		return p;
	}
	// Measured M4 reference contact, also explicitly rebased by M16. The
	// receiver-specific travel, rest pose and release paddle remain local.
	inline constexpr hands::anchor contact_reference{{-.80446699f,0,4.50075705f},{0,0,0,1}};
	inline constexpr hands::vec contact_low{-1.1f,-1.3f,3.8f},contact_high{.4f,1.3f,4.9f};
}
