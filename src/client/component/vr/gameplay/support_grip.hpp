#pragma once
#include "../controller_input.hpp"
#include "weapon_holding.hpp"
#include <cmath>

namespace vr::gameplay::weapons
{
	// Bounded, runtime-independent support lease. Only a NEW squeeze near the
	// anchor acquires it; holding squeeze while moving in must never auto-grab.
	class support_grip
	{
	  public:
		bool engaged() const noexcept { return engaged_; }
		hand consume(const controller_input::frame& input, const hold& owner, std::uint64_t assembly,
					 bool available, float distance_meters, float acquire, float release,
					 controller_input::clock::time_point now) noexcept
		{
			if (!available || !owner.can_fire() || !input.focused || !input.sequence ||
				now < input.sampled_at || now - input.sampled_at > std::chrono::milliseconds(150) ||
				!std::isfinite(distance_meters) || distance_meters < 0 || !std::isfinite(acquire) ||
				!std::isfinite(release) || acquire <= 0 || release <= acquire)
			{
				reset();
				return hand::none;
			}
			const int index = 1 - static_cast<int>(owner.rear);
			const auto& button = input.squeeze[index];
			if (continuity_.update(input,now) || owner.id() != weapon_ || owner.rear != rear_ || assembly != assembly_ ||
				input.reference_generation != reference_ || input.sequence < sequence_ || !button.active ||
				button.generation != generation_ || button.presses < presses_ || !input.grip[index].valid ||
				!input.aim[index].valid)
				reset();
			weapon_ = owner.id();
			rear_ = owner.rear;
			assembly_ = assembly;
			reference_ = input.reference_generation;
			sequence_ = input.sequence;
			const bool pressed = button.presses != presses_;
			generation_ = button.generation;
			presses_ = button.presses;
			if (!button.active || !input.grip[index].valid || !input.aim[index].valid)
				return hand::none;
			if (!button.down)
			{
				engaged_ = false;
				armed_ = true;
			}
			else if (engaged_ && distance_meters > release)
			{
				engaged_ = false;
				armed_ = false;
			}
			else if (armed_ && pressed)
			{
				engaged_ = distance_meters <= acquire;
				armed_ = false;
			}
			return engaged_ ? static_cast<hand>(index) : hand::none;
		}
		void reset() noexcept
		{
			armed_ = engaged_ = false;
			sequence_ = 0;
		}

	  private:
		bool armed_{}, engaged_{};
		weapon_identity weapon_{};
		hand rear_{hand::none};
		std::uint64_t assembly_{}, reference_{}, sequence_{}, generation_{}, presses_{};
		controller_input::consumer_continuity continuity_;
	};
} // namespace vr::gameplay::weapons
