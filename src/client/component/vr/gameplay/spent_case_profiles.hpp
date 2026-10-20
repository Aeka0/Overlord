#pragma once
#include "spent_case_geometry.hpp"
#include "native_weapon_family.hpp"

namespace vr::gameplay::weapons::spent_case
{
	struct source_recipe
	{
		const char* source;
		const char* bone;
		const recipe* geometry;
	};
	// Append only: one-based indices are persisted in casing FX checkpoint names.
	// Each skin retains its actual loaded materials and validates its own round.
	inline constexpr std::array<source_recipe, 8> sources{{
	    {"h2_viewmodel_cheytac_base", "j_bullet", &cheytac},
	    {"h2_viewmodel_cheytac_base_desert", "j_bullet", &cheytac},
	    {"h2_viewmodel_m82_base", "tag_bullet_single", &barrett},
	    {"h2_viewmodel_desert_eagle_base", "j_bullet01", &deserteagle},
	    {"h2_viewmodel_desert_eagle_gold", "j_bullet01", &deserteagle},
	    {"h2_viewmodel_wa2000_base", "j_ammo", &wa2000},
	    {"h2_viewmodel_m14ebr_base", "j_bullet", &m14},
	    {"h2_viewmodel_m14ebr_base_arctic", "j_bullet", &m14},
	}};
	inline unsigned variant_for(std::string_view weapon, std::string_view receiver = {}) noexcept
	{
		const auto select = [&](unsigned ordinary, unsigned alternate, bool named_alternate)
		{
			if (!receiver.empty())
			{
				if (receiver == sources[ordinary - 1].source)
					return ordinary;
				if (receiver == sources[alternate - 1].source)
					return alternate;
				return 0u;
			}
			return named_alternate ? alternate : ordinary;
		};
		if (native_weapon_family(weapon, "barrett"))
			return receiver.empty() || receiver == sources[2].source ? 3 : 0;
		if (native_weapon_family(weapon, "cheytac"))
			return select(1, 2, weapon.ends_with("_desert"));
		if (weapon == "deserteagle" || weapon == "deserteagle_gold")
			return select(4, 5, weapon == "deserteagle_gold");
		if (native_weapon_family(weapon, "wa2000"))
			return receiver.empty() || receiver == sources[5].source ? 6 : 0;
		if (native_weapon_family(weapon, "m14") || native_weapon_family(weapon, "m21") ||
		    weapon == "m14ebr_thermal")
			return select(7, 8, weapon.find("_arctic") != weapon.npos);
		return 0;
	}
}
