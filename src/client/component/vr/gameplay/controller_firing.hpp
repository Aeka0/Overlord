#pragma once
#include "component/vr/digital_button_gate.hpp"
#include "weapon_holding.hpp"

namespace vr::gameplay::weapons
{
	inline constexpr int attack_button = 0x1; // H2 +attack -> 0x1403CEDFC
	class trigger_policy
	{
	  public:
		bool consume(const controller_input::frame& input, const hold& owner, bool gameplay,
					 bool muzzle_ready, controller_input::clock::time_point now,bool discrete=false) noexcept
		{
			if (!gameplay || !muzzle_ready || !owner.can_fire() || !input.focused || !input.sequence ||
				now < input.sampled_at || now - input.sampled_at > std::chrono::milliseconds(150))
			{
				reset();
				return false;
			}
			if (continuity_.update(input,now) || owner.id() != owner_.id() || owner.rear_revision != owner_.rear_revision ||
				owner.rear != owner_.rear || input.reference_generation != reference_ ||
				input.sequence < sequence_)
				reset();
			owner_ = owner;
			reference_ = input.reference_generation;
			sequence_ = input.sequence;
			const auto index = static_cast<int>(owner.rear);
			if (!input.grip[index].valid || !input.aim[index].valid)
			{
				gate_ = {};
				return false;
			}
			const bool pressed=input.trigger[index].presses!=gate_.presses;
			return gate_.consume(input.trigger[index]) && (!discrete || pressed);
		}
		void reset() noexcept
		{
			gate_ = {};
			sequence_ = 0;
		}

	  private:
		controller_input::digital_button_gate gate_;
		controller_input::consumer_continuity continuity_;
		hold owner_{};
		std::uint64_t sequence_{}, reference_{};
	};
} // namespace vr::gameplay::weapons
