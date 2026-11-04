#pragma once
#include "weapon_carry_grip.hpp"
#include "../controller_input.hpp"

namespace vr::gameplay::weapons::carry
{
	inline result back_slot_candidate(const inventory& owned, hand actor, const holsters& slots,
	                                  hands::vec point, bool allow_storage) noexcept
	{
		if (!valid_hand(actor)) return {};
		if (const auto* held = owned.in_hand(actor))
		{
			if (!inventory::accepts(*held, location::back) || hit(slots, point) != location::back) return {};
			// Reuse the release rules without changing ownership or preparing a native transfer.
			auto preview = owned;
			const auto change = preview.release(held->id, 1u << unsigned(actor), location::back, true,
			                                    [](const instance&) { return false; }, false, allow_storage);
			if (change.action == outcome::stowed || change.action == outcome::exchanged) return change;
			return {};
		}
		if (draw_contact(owned, slots, point) != location::back) return {};
		return {outcome::drawn, owned.next_back()->id};
	}

	class back_slot_feedback
	{
	public:
		void reset() noexcept { *this = {}; }
		// Near includes a small exit margin. An operation consumes this visit too,
		// so the newly drawn/exchanged weapon cannot immediately emit another cue.
		template<class Ready>
		unsigned update(const controller_input::frame& input, unsigned near_hands, unsigned operated, Ready&& ready)
		{
			if (reference_ != input.reference_generation || continuity_ != input.continuity_generation)
				reset();
			reference_ = input.reference_generation;
			continuity_ = input.continuity_generation;
			unsigned pulses{};
			for (unsigned h = 0; h < 2; ++h)
			{
				const auto bit = 1u << h;
				if (!input.focused || input.orientation_settling || !input.sequence ||
				    !input.grip[h].valid || !input.aim[h].valid || !(near_hands & bit))
				{
					latched_ &= ~bit;
					continue;
				}
				if (operated & bit) latched_ |= bit;
				if (!(latched_ & bit) && ready(hand(h)))
				{
					latched_ |= bit;
					pulses |= bit;
				}
			}
			return pulses;
		}
	private:
		std::uint64_t reference_{}, continuity_{};
		unsigned latched_{};
	};
}
