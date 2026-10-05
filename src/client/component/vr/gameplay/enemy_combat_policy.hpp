#pragma once
#include "../settings.hpp"
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace vr::gameplay::enemy_combat
{
	inline bool enemy_melee(bool vr_enabled, bool player_target, bool actor_attacker,
		unsigned player_team, unsigned attacker_team, unsigned means_of_death) noexcept
	{
		// Both native melee kinds share the player's melee multiplier. An actor
		// actually attacking the player may be neutral (e.g. the museum), but
		// friendly teams and world/script damage must keep their original policy.
		return vr_enabled && player_target && actor_attacker && player_team && attacker_team &&
			player_team != attacker_team && (means_of_death == 8 || means_of_death == 9);
	}

	inline int scale_damage(int damage, float scale) noexcept
	{
		if (damage <= 0 || !std::isfinite(scale)) return damage;
		scale = std::clamp(scale, settings::enemy_melee_damage.min, settings::enemy_melee_damage.max);
		return static_cast<int>(std::clamp(std::round(double(damage) * scale),
			1.0, double(std::numeric_limits<int>::max())));
	}

	struct dog_branch {std::size_t source, target;};
	inline std::optional<dog_branch> bite_branch(std::span<const std::uint8_t> code) noexcept
	{
		// dog_cant_kill_in_one_hit: CHECK_CLEAR_PARAMS; isdefined(self.
		// meleeingplayer.dogs_dont_instant_kill); JUMP_ON_FALSE +3; GET_BYTE 1;
		// RETURN. Reuse the native early return after parameter validation.
		constexpr std::array<std::uint8_t,18> prefix{
			0x32,0x18,0x8e,0x5b,0x33,0x51,0xdc,0x2c,0x1b,0x2f,0x00,0xb0,0x03,0x00,0xa0,0x01,0x19,0x53};
		if (code.size() != 126 || !std::equal(prefix.begin(), prefix.end(), code.begin())) return {};
		return dog_branch{1,14};
	}
}
