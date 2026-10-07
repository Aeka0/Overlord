#pragma once
#include "native_grenade.hpp"
#include "native_carry.hpp"

namespace vr::gameplay::grenades::throwback
{
	struct candidate
	{
		weapons::native_carry::world_key entity{},owner{};
		native::descriptor grenade{};
		hands::vec position{};
		int deadline{};
		std::uint64_t timeline{};
		explicit operator bool()const noexcept{return entity.entity>0 && grenade.weapon!=0 && timeline!=0;}
	};
	bool initialize();
	bool ready()noexcept;
	candidate query()noexcept;
	// Rechecks the exact HUD entity/generation before calling native pickup.
	// Success transfers one world missile, never inventory ammunition.
	bool take(const candidate&)noexcept;
	// Resolve the original thrower only for an expired held/retried grenade.
	int expired_owner(const candidate&)noexcept;
}
