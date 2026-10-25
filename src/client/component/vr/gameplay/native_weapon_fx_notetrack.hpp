#pragma once
#include "game/assets.hpp"
#include <utils/native_memory.hpp>
#include <array>
#include <string_view>

namespace vr::gameplay::native_fx
{
	// Some manual weapons author view brass only in the animation FX map. Resolve
	// their explicit extraction note on the native owner thread; never play the
	// animation or borrow another weapon's effect. No asset pointers are retained.
	template<class Matches>
	game::FxEffectDef* notetrack_effect(const game::WeaponDef* definition,
	                                  std::string_view note,
	                                  std::string_view tag,
	                                  Matches matches) noexcept
	{
		static_assert(offsetof(game::WeaponDef, notetrackFXMapKeys) == 0xc0);
		static_assert(offsetof(game::WeaponDef, notetrackFXMapValues) == 0xc8);
		static_assert(offsetof(game::WeaponDef, notetrackFXMapTagValues) == 0xd0);
		if (note.empty() || tag.empty()) return nullptr;
		const game::scr_string_t *keys{}, *tags{};
		game::FxEffectDef* const* values{};
		std::array<game::scr_string_t, 16> copied_keys{}, copied_tags{};
		std::array<game::FxEffectDef*, 16> copied_values{};
		using utils::native_memory::read_at;
		using utils::native_memory::read_bytes;
		if (!read_at(definition, offsetof(game::WeaponDef, notetrackFXMapKeys), keys) ||
		    !read_at(definition, offsetof(game::WeaponDef, notetrackFXMapValues), values) ||
		    !read_at(definition, offsetof(game::WeaponDef, notetrackFXMapTagValues), tags) ||
		    !read_bytes(copied_keys.data(), keys, sizeof(copied_keys)) ||
		    !read_bytes(copied_values.data(), values, sizeof(copied_values)) ||
		    !read_bytes(copied_tags.data(), tags, sizeof(copied_tags))) return nullptr;
		game::FxEffectDef* result{};
		for (std::size_t i = 0; i < copied_keys.size(); ++i)
		{
			if (!copied_keys[i] || !matches(copied_keys[i], note)) continue;
			if (result || !copied_values[i] || !copied_tags[i] ||
			    !matches(copied_tags[i], tag)) return nullptr;
			result = copied_values[i];
		}
		return result;
	}
}
