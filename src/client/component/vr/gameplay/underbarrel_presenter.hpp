#pragma once
#include "underbarrel_runtime.hpp"
#include "falling_item_presenter.hpp"
#include <string>

namespace vr::gameplay::weapons::underbarrel::round_presentation
{
	struct extraction
	{
		weapon_identity owner{};
		std::uint64_t reference{};
		clock::time_point at{};
		hands::anchor world{};
		float units{};
		bool digital{};
	};
	inline falling_item_presentation::motion case_flight(const extraction& event) noexcept
	{
		falling_item_presentation::motion result;
		result.path.start = event.world;
		// The unsupported case falls under world gravity; no ejector impulse.
		result.path.born = event.at;
		result.path.units = event.units;
		return result;
	}
	// Only an accepted server opening may release a case. The bounded cosmetic
	// queue carries no live asset pointers and never writes ammunition.
	void eject(const extraction&) noexcept;
	float case_clearance_units(bool digital) noexcept;
	// Called after the exact held-weapon skeleton is solved. Seated rounds are
	// independent rigid subsets, so a held spare cannot steal chamber geometry.
	void present(void* object,
	             std::uint32_t epoch,
	             const void* matrices,
	             const part_rig&,
	             const presentation&,
	             const hold&,
	             const controller_input::frame&,
	             hands::anchor gun,
	             hands::vec offset,
	             float units,
	             bool gameplay) noexcept;
	std::string status();
}
