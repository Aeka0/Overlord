#pragma once
#include <string_view>

namespace vr::gameplay::weapons
{
	// A notetrack key and a loaded sound alias are different namespaces. Never
	// guess a direct alias after a failed WeaponDef lookup or ship extracted audio.
	enum class sound_reference_kind { notetrack, alias };
	enum class sound_part { whole, first, second };
	// Immutable, recording-specific range. Never apply it to an alias-selected
	// variant until that recording's format has passed the cue lookup.
	struct sound_window {std::string_view recording;unsigned begin_ms{},end_ms{};};
	struct sound_reference
	{
		const char* name{};
		sound_reference_kind kind{sound_reference_kind::notetrack};
		sound_part part{sound_part::whole};
		// Optional, explicit source WeaponDef for shared physical-reload keys.
		// The playing weapon still passes its own identity/ownership admission.
		const char* notetrack_weapon{};
		const sound_window* window{};
	};
}
