#pragma once

#include <cstdint>
#include "weapon_identity.hpp"
#include "../hand.hpp"

namespace vr::gameplay::weapons
{
enum class grip_role
	{
		rear,
		support
	};
	enum class control_attachment { fixed, moving };
	enum class hold_source
	{
		engine_default,
		interaction
	};
struct hold
	{
		std::uint32_t weapon{};
		std::uint64_t revision{};
		hand rear{hand::none};
		hand support{hand::none};
		hold_source source{hold_source::engine_default};
		std::uint64_t rear_revision{}; // Support changes must not rearm the firing hand.
		hand pose_rear{hand::right}; // Last control side while carried by the foregrip alone.
		std::uint64_t instance_generation{};
		control_attachment attachment{control_attachment::fixed}; // Selected control contact; revision-bound.
		weapon_identity id() const noexcept { return {weapon,instance_generation}; }
		hand holding_hand() const noexcept
		{return weapon ? (valid_hand(rear) ? rear : support) : hand::none;}
		hand manipulation_hand() const noexcept
		{
			const auto holder=holding_hand();
			if (!valid_hand(holder) || (valid_hand(rear) && valid_hand(support))) return hand::none;
			return static_cast<hand>(1-static_cast<int>(holder));
		}
		bool can_fire() const noexcept
		{
			return weapon != 0 && valid_hand(rear);
		}
	};

	// One authority for both weapon pose and trigger routing. Future rear-grip
	// detection commits a validated interaction; supporting a foregrip is not ownership.
	class holding_state
	{
	  public:
		hold current() const noexcept
		{
			return value_;
		}
		hold equipped(std::uint32_t weapon) noexcept
		{
			if (weapon != value_.weapon)
				value_ = {weapon,	  value_.revision + 1,		   weapon ? hand::right : hand::none,
						  hand::none, hold_source::engine_default, value_.rear_revision + 1};
			return value_;
		}
		bool grip(const hold& expected, grip_role role, hand owner) noexcept
		{
			if (!value_.weapon || expected.id() != value_.id() || expected.revision != value_.revision ||
				(owner != hand::none && !valid_hand(owner)) ||
				(role != grip_role::rear && role != grip_role::support))
				return false;
			if (role == grip_role::support && owner != hand::none && owner == value_.rear)
				return false;
			auto next = value_;
			if (role == grip_role::rear)
			{
				next.rear = owner;
				if (next.rear != value_.rear)
					++next.rear_revision;
				if (owner != hand::none && owner == next.support)
					next.support = hand::none;
			}
			else
				next.support = owner;
			next.source = hold_source::interaction;
			if (next.rear != value_.rear || next.support != value_.support || next.source != value_.source)
				++next.revision;
			value_ = next;
			return true;
		}

	  private:
		hold value_{};
	};
} // namespace vr::gameplay::weapons
