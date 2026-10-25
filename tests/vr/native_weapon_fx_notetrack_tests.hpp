#pragma once
#include "component/vr/gameplay/native_weapon_fx_notetrack.hpp"

template<class Check> void native_weapon_fx_notetrack_tests(Check check)
{
	using vr::gameplay::native_fx::notetrack_effect;
	game::WeaponDef weapon{};
	game::FxEffectDef shell{}, empty_reload{};
	std::array<game::scr_string_t, 16> keys{}, tags{};
	std::array<game::FxEffectDef*, 16> values{};
	weapon.notetrackFXMapKeys = keys.data();
	weapon.notetrackFXMapValues = values.data();
	weapon.notetrackFXMapTagValues = tags.data();
	const std::array<std::string_view, 7> strings{
	    "", "spas12_shelleject", "w1200_rechamber", "w1200_rechamber_empty", "tag_brass",
	    "model1887_reload", "tag_origin"};
	const auto matches = [&](game::scr_string_t id, std::string_view name)
	    { return id < strings.size() && strings[id] == name; };
	for (unsigned note : {1u, 2u})
	{
		// Captured H2 pump definitions leave both ordinary view-brass fields null.
		keys[0] = note; tags[0] = 4; values[0] = &shell;
		keys[1] = 3; tags[1] = 4; values[1] = &empty_reload;
		check(!weapon.viewShellEjectEffect && !weapon.viewLastShotEjectEffect &&
		      notetrack_effect(&weapon, strings[note], "tag_brass", matches) == &shell,
		      "pump case resolves its extraction note without the empty-reload effect");
		keys[15] = note; tags[15] = 4; values[15] = &shell;
		check(!notetrack_effect(&weapon, strings[note], "tag_brass", matches), "ambiguous extraction notes rejected");
		keys[15] = 0;
		tags[0] = 3;
		check(!notetrack_effect(&weapon, strings[note], "tag_brass", matches), "non-brass attachment rejected");
		tags[0] = 4; values[0] = nullptr;
		check(!notetrack_effect(&weapon, strings[note], "tag_brass", matches), "missing effect never borrows a neighboring note");
	}
	keys[0] = 5; tags[0] = 6; values[0] = &shell;
	check(notetrack_effect(&weapon, strings[5], "tag_origin", matches) == &shell &&
	      !notetrack_effect(&weapon, strings[5], "tag_brass", matches),
	      "M1887 resolves its own origin-attached note without treating it as a brass-tag effect");
	check(!notetrack_effect(&weapon, "", "tag_brass", matches) &&
	      !notetrack_effect(&weapon, "unknown", "tag_brass", matches), "unconfigured and unknown notes do not emit");
	weapon.notetrackFXMapKeys = nullptr;
	check(!notetrack_effect(&weapon, strings[1], "tag_brass", matches) &&
	      !notetrack_effect(nullptr, strings[1], "tag_brass", matches), "missing native maps fail safely");
}
