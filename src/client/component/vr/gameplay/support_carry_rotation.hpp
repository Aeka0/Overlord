#pragma once
#include "hands/pose_solver.hpp"
#include "weapon_holding.hpp"
#include <optional>

namespace vr::gameplay::weapons::carry
{
	// Captured by the ownership transaction; shared by render and same-frame
	// mechanical sampling. A sole foregrip carries the gun's relative orientation.
	struct support_carry_rotation
	{
		weapon_identity weapon{};
		std::uint64_t revision{},reference{};
		hand actor{hand::none};
		hands::quat local{0,0,0,1};
		bool capture(const hold& before,const hold& after,std::uint64_t space,
			hands::quat controller,hands::quat gun) noexcept
		{
			*this={};
			if(!space || !before.can_fire() || before.id()!=after.id() || after.can_fire() ||
				!valid_hand(after.support) || before.support!=after.support)return false;
			for(const auto& q:{controller,gun})
			{float n{};for(float x:q)n+=x*x;if(!std::isfinite(n) || n<.5f || n>1.5f)return false;}
			weapon=after.id();revision=after.rear_revision;reference=space;actor=after.support;
			local=hands::normalize(hands::multiply(hands::conjugate(hands::normalize(controller)),hands::normalize(gun)));
			return true;
		}
		std::optional<hands::quat> basis(const hold& owner,std::uint64_t space)const noexcept
		{
			if(!weapon || owner.id()!=weapon || owner.rear_revision!=revision || owner.can_fire() ||
				owner.support!=actor || space!=reference)return std::nullopt;
			return local;
		}
	};
}
