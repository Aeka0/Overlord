#pragma once
#include "chambering_guide_assets.hpp"
#include "weapon_holding.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::weapons::chambering_guide
{
	struct posed_part {part asset;hands::anchor pose;};
	struct sample
	{
		std::uintptr_t object{},matrices{};
		std::uint32_t epoch{};
		hold owner{};
		std::uint64_t instance{},reference{};
		controller_input::clock::time_point at{};
		const reload_profile* reload{};
		const tube_profile* tube{};
		std::array<posed_part,6> parts{};
		size_t count{};
		void add(const part& asset,hands::anchor pose) noexcept
		{if(asset.geometry && count<parts.size())parts[count++]={asset,pose};}
	};
	// Mechanical presenters publish exact solved geometry, including an empty
	// sample when no cue is needed. One shared consumer owns both eyes/lifetime.
	void publish(const sample&) noexcept;
	std::string render_status();
}
