#pragma once
#include "controller_input.hpp"

namespace vr::controller_input
{
	// One deliberate edge, unlike the level gate used by automatic fire.
	// Reconnection/ownership changes require a neutral sample before arming.
	struct digital_press_gate
	{
		bool armed{};
		std::uint64_t generation{}, presses{};
		bool consume(const digital_action& value) noexcept
		{
			if (!value.active || value.generation != generation || value.presses < presses) armed = false;
			const bool pressed = value.presses != presses;
			generation = value.generation;
			presses = value.presses;
			if (!armed)
			{
				armed = value.active && !value.down;
				return false;
			}
			return pressed;
		}
	};

	struct digital_button_gate
	{
		bool armed{};
		std::uint64_t generation{}, presses{};
		bool consume(const digital_action& value) noexcept
		{
			if (!value.active || value.generation != generation || value.presses < presses) armed = false;
			const bool pressed = value.presses != presses;
			generation = value.generation;
			presses = value.presses;
			if (!armed)
			{
				armed = value.active && !value.down;
				return false;
			}
			return value.down || pressed;
		}
	};
	struct paired_button_result {bool held{},pressed{};};
	struct paired_button_gate
	{
		std::array<digital_button_gate,2> hands{};
		paired_button_result consume(const std::array<digital_action,2>& input)noexcept
		{
			paired_button_result result;
			for(unsigned h=0;h<hands.size();++h)
			{
				const bool changed=input[h].presses!=hands[h].presses;
				if(hands[h].consume(input[h])){result.held=true;result.pressed|=changed;}
			}
			return result;
		}
	};
}
