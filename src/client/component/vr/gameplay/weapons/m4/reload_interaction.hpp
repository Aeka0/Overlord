#pragma once
#include "../../families/ar.hpp"

namespace vr::gameplay::weapons::m4
{
	// Metres; native pullout_first handle travel is 5.82 cm along gun-local -X.
	// The handle returns forward independently of bolt lock. Contact dimensions
	// are authored candidates; registering these without verified part visibility
	// would enable invisible/duplicated magazines, so admission is separate.
	// Receiver SHA-256 90307e116e650ae82a652c24bac94b8ef9fcec4af19f9b097e82a3c56fd3ff44.
	// Upper paddle of j_clip_release, mesh-reviewed left (+Y) face, cm / 2.54.
	inline constexpr physical_reload::receiver_bolt_release bolt_release{.centre={3.34130357f,.78309145f,3.19522234f},.visual_bone="j_clip_release"};
	inline constexpr auto reload_interaction=families::ar::charging_handle(.059f,.053f,&bolt_release);
	inline constexpr float handle_return_seconds=.075f;
	static_assert(physical_reload::native_action_recoil(physical_reload::profile{})==true);
}
