#pragma once
#include <cstdint>

namespace vr::gameplay::weapons
{
	// A native token identifies a definition. Generation identifies one physical
	// gun across grips, storage and world transfers; zero is a legacy/native view.
	struct weapon_identity
	{
		std::uint32_t weapon{};
		std::uint64_t generation{};
		bool operator==(const weapon_identity&) const = default;
		explicit operator bool() const noexcept { return weapon && generation; }
	};
	inline bool projection_rebind(weapon_identity previous,weapon_identity next) noexcept
	{
		// Mode changes may rebind the unique legacy feed, never two physical guns.
		return previous.weapon && previous.weapon==next.weapon &&
			bool(previous.generation)!=bool(next.generation);
	}
}
