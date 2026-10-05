#include "weapon_mechanics_profiles.hpp"
#include "weapons/m9/mechanics.hpp"
#include "weapons/m1911/mechanics.hpp"
#include "weapons/de50/mechanics.hpp"
#include "weapons/usp/mechanics.hpp"
#include "weapons/g18/mechanics.hpp"
#include "weapons/m93r/mechanics.hpp"
#include "weapons/tmp/mechanics.hpp"
namespace vr::gameplay::weapons
{
	const closed_bolt::rules* native_chamber_profile(std::string_view name, int capacity) noexcept
	{
		// Explicit opt-in. Future weapons author rules beside their grips/actions;
		// never infer closed-bolt behavior from clip size alone.
		if (name == m9::native_name && capacity == m9::chamber_rules.magazine_capacity)
			return &m9::chamber_rules;
		if (name == m1911::native_name && capacity == m1911::chamber_rules.magazine_capacity)
			return &m1911::chamber_rules;
		if (name == de50::native_name && capacity == de50::chamber_rules.magazine_capacity)
			return &de50::chamber_rules;
		if (name == g18::native_name && capacity == g18::chamber_rules.magazine_capacity)
			return &g18::chamber_rules;
		if (name == m93r::native_name && capacity == m93r::chamber_rules.magazine_capacity)
			return &m93r::chamber_rules;
		if (tmp::native_variant(name) && capacity == tmp::chamber_rules.magazine_capacity)
			return &tmp::chamber_rules;
		if ((name == usp::native_name || name == usp::silenced_native_name) && capacity == usp::chamber_rules.magazine_capacity)
			return &usp::chamber_rules;
		return nullptr;
	}
}
