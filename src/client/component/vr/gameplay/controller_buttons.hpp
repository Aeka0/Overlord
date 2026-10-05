#pragma once

#include "../controller_input.hpp"
#include "component/vr/digital_button_gate.hpp"

namespace vr::controller_input
{
	// H2 command bits verified through native key whitelist -> key dispatch ->
	// button packing, not copied from another CoD's usercmd definition.
	inline constexpr int sprint_button = 0x2;
	inline constexpr int jump_button = 0x400;

	struct button_commands
	{
		int held{};    // Native command bits, including a short tap between samples.
		int pressed{}; // Accepted new presses only, for native script notifications.
	};

	class command_buttons
	{
	public:
		button_commands consume(const frame& input, bool gameplay, clock::time_point now) noexcept
		{
			if (!gameplay || !input.focused || input.sequence == 0 || now < input.sampled_at ||
				now - input.sampled_at > std::chrono::milliseconds(150))
			{
				reset();
				return {};
			}
			if (input.reference_generation != reference_generation_ || input.sequence < sequence_ ||
				now < last_time_ || now - last_time_ > std::chrono::milliseconds(150)) reset();
			reference_generation_ = input.reference_generation;
			sequence_ = input.sequence;
			last_time_ = now;
			const int changed = (input.sprint.presses != sprint_.presses ? sprint_button : 0) |
				(input.jump.presses != jump_.presses ? jump_button : 0);
			const int held = (sprint_.consume(input.sprint) ? sprint_button : 0) |
				(jump_.consume(input.jump) ? jump_button : 0);
			// The level gates own admission/rearming for both outputs. A changed
			// counter from a rejected generation must never become a script event.
			return {held, held & changed};
		}

		void reset() noexcept
		{
			sprint_ = {};
			jump_ = {};
			sequence_ = 0;
		}

	private:
		digital_button_gate sprint_;
		digital_button_gate jump_;
		std::uint64_t reference_generation_{};
		std::uint64_t sequence_{};
		clock::time_point last_time_{};
	};
}
