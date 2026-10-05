#pragma once
#include "../weapon_identity.hpp"

namespace vr::gameplay::hand_interaction
{
	// Provider-local identity. Its validity belongs to the provider: initial
	// world entities may have generation zero and head gestures have no token.
	struct object_identity
	{
		std::uint32_t value{};
		std::uint64_t generation{};
		constexpr object_identity() = default;
		constexpr object_identity(std::uint32_t value, std::uint64_t generation) noexcept
			: value(value), generation(generation) {}
		constexpr object_identity(weapons::weapon_identity id) noexcept
			: value(id.weapon), generation(id.generation) {}
		bool operator==(const object_identity&) const = default;
	};
}
