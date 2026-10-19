#pragma once
#include "native_bullet_trace.hpp"
#include "native_weapon_family.hpp"
#include <cmath>
#include <string_view>

namespace vr::gameplay::weapons::hei_round
{
	inline bool grenade_weapon(int type, int weapon_class, int inventory, std::string_view name) noexcept
	{
		if (type != 1)
			return false;
		if (weapon_class == 4 && (inventory == 0 || inventory == 3))
			return true; // Primary and underbarrel shotguns.
		if (inventory != 0)
			return false;
		// Match native inventory identities, as the reload profiles do. "m82"
		// names the exported model family; the carried weapon is "barrett".
		for (std::string_view family : {"cheytac", "wa2000", "barrett"})
			if (native_weapon_family(name, family))
				return true;
		return false;
	}
	// One addition per discharged round, shared by all pellets and penetration
	// segments. Invalid/missed segments do not consume the round's contact.
	struct first_contact
	{
		bool found{};
		spatial_math::vec position{}, normal{};
		bool record(const native_bullet_trace::result& hit) noexcept
		{
			if (found || !hit.type || hit.entity == 0 || hit.ignored || (hit.surface & 0x14) ||
			    !std::isfinite(hit.fraction) || hit.fraction < 0 || hit.fraction >= 1)
				return false;
			for (const auto v : {hit.position, hit.normal})
				for (const float x : v)
					if (!std::isfinite(x) || std::abs(x) > 1.e7f)
						return false;
			position = hit.position;
			normal = hit.normal;
			return found = true;
		}
		spatial_math::vec center() const noexcept
		{
			float length{};
			for (float x : normal)
				length += x * x;
			const float gain = length > .0001f ? .5f / std::sqrt(length) : 0.f;
			auto value = position;
			for (std::size_t i{}; i < value.size(); ++i)
				value[i] += normal[i] * gain;
			return value;
		}
	};
}
