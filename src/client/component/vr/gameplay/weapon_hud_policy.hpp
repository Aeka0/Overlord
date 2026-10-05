#pragma once
#include "weapon_holding.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::weapon_hud
{
	// Visibility is a preference. Losing focus/ownership rearms input but never
	// flips the preference or replays a press from a menu/previous weapon.
	class toggle_policy
	{
	public:
		bool visible() const noexcept { return visible_; }
		void reset_input() noexcept { armed_ = false; sequence_ = 0; }
		bool consume(const controller_input::frame& input, const weapons::hold& owner,
			bool gameplay, controller_input::clock::time_point now) noexcept
		{
			if (!gameplay || !owner.can_fire() || !input.focused || !input.sequence ||
				now < input.sampled_at || now - input.sampled_at > std::chrono::milliseconds(150))
			{
				reset_input();
				return false;
			}
			// A is one global ammunition-HUD preference, even with a left-only
			// weapon. X remains available to the left hand's gameplay actions.
			constexpr auto hand = static_cast<int>(vr::hand::right);
			const auto& button = input.primary[hand];
			if (owner.id() != owner_.id() || owner.rear != owner_.rear ||
				owner.rear_revision != owner_.rear_revision || input.reference_generation != reference_ ||
				!sequence_ || input.sequence < sequence_ || now < last_time_ ||
				now - last_time_ > std::chrono::milliseconds(150) ||
				button.generation != generation_ || button.presses < presses_ ||
				!button.active || !input.grip[hand].valid)
				armed_ = false;
			const auto delta = button.presses - presses_;
			owner_ = owner;
			sequence_ = input.sequence;
			reference_ = input.reference_generation;
			generation_ = button.generation;
			presses_ = button.presses;
			last_time_ = now;
			if (!armed_)
			{
				armed_ = button.active && input.grip[hand].valid && !button.down;
				return false;
			}
			if (!(delta & 1)) return false;
			visible_ = !visible_;
			return true;
		}
	private:
		bool visible_{true}, armed_{};
		weapons::hold owner_{};
		std::uint64_t sequence_{}, reference_{}, generation_{}, presses_{};
		controller_input::clock::time_point last_time_{};
	};
}
