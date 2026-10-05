#pragma once
#include "player_life.hpp"
#include <utils/native_memory.hpp>
#include <array>

namespace vr::gameplay::player_life
{
	inline bool read(const void* ps,bool& is_dead) noexcept
	{
		static const bool verified=[] {
			constexpr std::array<unsigned char,7> health_test{0x83,0xbf,0xec,0x01,0,0,0};
			std::array<unsigned char,7> bytes{};
			return utils::native_memory::read_bytes(bytes.data(),reinterpret_cast<void*>(0x1404AD7A1),bytes.size()) && bytes==health_test;
		}();
		std::uint8_t type{};int health{};
		if(!verified || !ps || !utils::native_memory::read_bytes(&type,static_cast<const std::byte*>(ps)+2,1) ||
			!utils::native_memory::read_bytes(&health,static_cast<const std::byte*>(ps)+0x1ec,sizeof(health)))return false;
		is_dead=dead(type,health);return true;
	}
	inline bool dead(const void* ps) noexcept {bool value{};return read(ps,value) && value;}
}
