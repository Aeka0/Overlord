#pragma once
#include "weapon_holding.hpp"
#include "weapon_hud_channels.hpp"
#include <array>
#include <string>

namespace vr::gameplay::weapon_hud
{
	struct source_snapshot
	{
		weapons::hold owner{};
		std::uint64_t reference{};
		feed channel{};
		std::uint32_t definition{}; // Displayed feed; owner remains the carried host.
		int loaded{},reserve{},capacity{},maximum_reserve{},clip_type{};
		float low_threshold{};
		bool needs_chamber{};
		bool quick_loading{};
		std::string name;
	};
	// LUI gets copied read-only data; it never projects selection into native PS.
	source_snapshot source(unsigned index) noexcept;
	void source_rendered(unsigned index,bool visible) noexcept;
	std::array<source_snapshot,source_count> source_owners() noexcept;
}
