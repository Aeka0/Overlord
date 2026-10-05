#pragma once
#include "hand_interaction/frame.hpp"
#include "weapon_carry_runtime.hpp"
#include "grip_edges.hpp"

namespace vr::gameplay::interaction {struct target;}
namespace vr::gameplay::weapons::carry
{
	// Server-owned frame adapter. The coordinator reads this batch only during
	// the current update; no render consumer may retain it or native pointers.
	struct interaction_context
	{
		hand_interaction::frame frame{};
		controller_input::frame raw_input{};
		std::array<instance,inventory::capacity> previous{};
		std::array<identity,2> released{};
		std::array<hands::vec,2> hand_positions{};
		grip_edges edges{};
		holster_layout layout{};
		const void* player{};
	};
	interaction_context* prepare_interactions();
	void collect_interactions();
	unsigned apply_grips(unsigned claimed);
	unsigned world_use_hands() noexcept;
	bool pickup(const interaction::target&,int hand);
	std::span<const instance> interaction_instances() noexcept;
	bool release_support(identity) noexcept;
	bool acquire_support(identity,hand) noexcept;
	bool interaction_hand_occupied(hand) noexcept;
	void publish_topology() noexcept;
	void publish_interactions() noexcept;
	void refresh_interaction_objects();
	void finish_interactions();
}
