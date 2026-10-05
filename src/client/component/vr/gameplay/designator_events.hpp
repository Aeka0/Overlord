#pragma once
#include "weapon_interaction.hpp"
#include "designator_event_policy.hpp"
namespace vr::gameplay::equipment::special::designator_events
{
	void record_shot(const weapons::muzzle_frame&)noexcept;
	bool ready()noexcept;
	bool can_fire()noexcept;
	activation_state activation(std::uint32_t weapon)noexcept;
	bool request_activation(std::uint32_t weapon)noexcept;
	bool owns_native_transition()noexcept;
}
