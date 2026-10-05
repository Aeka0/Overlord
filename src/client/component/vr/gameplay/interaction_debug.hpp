#pragma once
#include "interaction_volume.hpp"
#include "body_supply_volume.hpp"
#include "weapon_holsters.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::interaction::debug
{
	using clock=controller_input::clock;
	enum class verdict {geometry,occluded,admitted,selected,native_rejected};
	enum class visibility {not_tested,blocked,clear};
	struct candidate
	{
		target_key key{};std::uint32_t weapon{};oriented_volume volume{};vec native_center{},native_half{};
		target scored{};verdict result{};
		vec trace_point{};visibility head_visibility{},hand_visibility{};bool script_model{};
	};
	struct world_sample
	{
		ray aim{};std::array<candidate,8> candidates{};unsigned count{},native_count{};target selected{};
		std::uint64_t reference{};clock::time_point at{};
	};
	struct body_sample
	{
		weapons::carry::holsters slots{};std::array<vec,2> hands{};std::array<std::uint32_t,3> weapons{};
		float units{};std::uint64_t reference{};clock::time_point at{};
	};
	struct supply_sample
	{
		std::array<supply_volume,2> volumes{};vec wrist{};weapons::hold owner{};
		float units{};int hand{-1};std::uint64_t reference{};clock::time_point at{};
	};
	bool world_enabled() noexcept;
	bool body_enabled() noexcept;
	void publish_world(unsigned hand,const world_sample&) noexcept;
	void publish_body(const body_sample&) noexcept;
	void publish_supply(const supply_sample&) noexcept;
}
