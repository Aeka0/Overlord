#pragma once
#include "grenade_state.hpp"
#include "hand_interaction/core.hpp"

namespace vr::gameplay::grenades
{
	enum class trigger_action
	{
		none,
		bite,
		cook
	};

	// Consume against the phase at the start of the transaction. A pin removed
	// later in this frame cannot reinterpret the same press as a cook command.
	class holder_trigger
	{
		bool ready_{};

	  public:
		void reset() noexcept
		{
			ready_ = false;
		}
		trigger_action consume(phase before, bool active, hand_interaction::edges input) noexcept
		{
			if (!active || !input.armed)
			{
				reset();
				return trigger_action::none;
			}
			if (!input.down)
				ready_ = true;
			if (!ready_ || !input.press || !input.down || input.release)
				return trigger_action::none;
			ready_ = false;
			return before == phase::safe       ? trigger_action::bite
			       : before == phase::prepared ? trigger_action::cook
			                                   : trigger_action::none;
		}
	};
}
