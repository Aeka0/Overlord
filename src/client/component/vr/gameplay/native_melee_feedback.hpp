#pragma once
#include "body_equipment.hpp"
#include "melee_weapon_policy.hpp"
#include <string>

namespace vr::gameplay::melee::feedback
{
	bool initialize();
	// Accepted server strike only. Copies the real knife root; resolves native
	// FX/socket assets later on the client owner, never through the selected gun.
	bool knife(const hands::anchor& root, vr::hand hand, std::uint64_t revision, std::uint64_t reference,
		weapons::weapon_identity weapon={},equipment::melee_weapons::kind variant=equipment::melee_weapons::kind::ghost);
	bool baton_sound(hands::vec position,vr::hand,std::uint64_t revision,std::uint64_t reference,bool impact);
	bool pickaxe(const hands::anchor& root,vr::hand hand,unsigned side,std::uint64_t lease,std::uint64_t reference);
	std::string status();
}
