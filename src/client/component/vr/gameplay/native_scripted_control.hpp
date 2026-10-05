#pragma once
#include "scripted_control.hpp"
#include "mounted_turret_policy.hpp"
#include "scripted_sequences.hpp"
#include "native_player_life.hpp"
#include "sequences/airport_opening.hpp"
#include <utils/native_memory.hpp>
#include "game/game.hpp"
#include <array>
#include <cstring>

namespace vr::gameplay::scripted_control
{
	inline bool native_contract() noexcept
	{
		// H2 method table 0x14B155890: disableweapons 0x8328 / enableweapons
		// 0x8329. Their paired instructions set/clear PS+0x3c0 bit 0x80.
		static const bool verified = [] {
			constexpr std::array<unsigned char,10> disable{0x81,0x8b,0xc0,0x03,0,0,0x80,0,0,0};
			constexpr std::array<unsigned char,10> enable{0x81,0xa0,0xc0,0x03,0,0,0x7f,0xff,0xff,0xff};
			std::array<unsigned char,10> a{}, b{};
			return utils::native_memory::read_bytes(a.data(),reinterpret_cast<const void*>(0x1404B706E),a.size()) &&
				utils::native_memory::read_bytes(b.data(),reinterpret_cast<const void*>(0x1404B73C2),b.size()) &&
				a == disable && b == enable;
		}();
		return verified;
	}
	inline bool allowed(const void* player_state) noexcept
	{
		std::uint32_t flags{},entity_flags{};
		bool dead{};
		return player_state && native_contract() && player_life::read(player_state,dead) && !dead &&
			utils::native_memory::read_bytes(&flags,static_cast<const std::byte*>(player_state)+0x3c0,sizeof(flags)) &&
			utils::native_memory::read_bytes(&entity_flags,static_cast<const std::byte*>(player_state)+offsetof(game::playerState_s,e_flags),sizeof(entity_flags)) &&
			(permits_weapons(flags) || sequences::airport::opening::active()) &&
			!mounted::attached(entity_flags) && !sequences::owns_body(player_state) && !sequences::owns_arms(player_state);
	}
	inline bool firing_allowed(const void* player_state) noexcept
	{
		return !sequences::airport::opening::active() && allowed(player_state);
	}
	inline bool predicted_allowed() noexcept
	{
		return game::CL_IsCgameInitialized() && allowed(game::CG_GetPredictedPlayerState(0));
	}
	inline bool predicted_firing_allowed() noexcept
	{
		return game::CL_IsCgameInitialized() && firing_allowed(game::CG_GetPredictedPlayerState(0));
	}
}
