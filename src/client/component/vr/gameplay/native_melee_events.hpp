#pragma once
#include <cstdint>
#include <cstring>
#include <span>

namespace vr::gameplay::melee::native::events
{
	inline constexpr int hit=0x3c,blood=0x3e;
	inline bool valid_weapon_hit(std::uint16_t target,std::uint16_t attacker,std::uint32_t weapon) noexcept
	{return target>0 && target<3998 && attacker==0 && weapon>0 && weapon<512;}
	// G_TempEntity's state prefix, as filled by native melee at 0x14051d33d.
	// Keep the allocator's trajectory, event type, entity number and lifetime.
	inline bool weapon_hit(std::span<std::byte> entity,std::uint16_t target,std::uint16_t attacker,
		std::uint32_t weapon,std::uint32_t player_flags,bool blade) noexcept
	{
		if (entity.size()<0x90 || !valid_weapon_hit(target,attacker,weapon)) return false;
		const auto put=[&]<class T>(std::size_t at,T value) {std::memcpy(entity.data()+at,&value,sizeof(value));};
		put(0x02,std::uint8_t{0}); // Native actor hit: no world surface override.
		put(0x08,std::uint32_t{8u|(blade?1u:0u)|((player_flags&0x10000u) ? 0x10u : 0u)});
		put(0x7c,attacker);put(0x80,weapon);put(0x84,std::uint32_t{0});put(0x8e,target);
		return true;
	}
	inline bool knife_hit(std::span<std::byte> entity,std::uint16_t target,std::uint16_t attacker,
		std::uint32_t weapon,std::uint32_t player_flags) noexcept
	{return weapon_hit(entity,target,attacker,weapon,player_flags,true);}
}
