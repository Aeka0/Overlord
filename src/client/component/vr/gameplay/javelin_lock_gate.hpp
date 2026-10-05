#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace vr::gameplay::weapons::javelin_screen
{
	// Exact statements in native 44133/BCBD. Its first playerads() tests full
	// ADS for locking; the second tests nonzero ADS for pickup suppression.
	// Return the VM continuation after the FIRST method's 16-bit operand only.
	inline std::optional<std::size_t> full_ads_call(std::span<const std::uint8_t> code,std::uint16_t method) noexcept
	{
		if(code.size()<40 || code.size()>128 || code[0]!=0x2c || code[1]!=0 || code[2]!=0x32 || code.back()!=0x34)return {};
		unsigned methods{},full{},partial{};std::size_t continuation{};
		for(std::size_t i=0;i+4<=code.size();++i)
		{
			if(code[i]!=0x41 || code[i+1]!=0xa9 || code[i+2]!=(method&255) || code[i+3]!=(method>>8))continue;
			++methods;
			constexpr std::uint8_t complete[]{0x2b,0,0,0x80,0x3f,0x61,0xb0,2,0,0x79,0x19};
			constexpr std::uint8_t nonzero[]{0x79,0x90,0xb0,2,0,0x79,0x19};
			const auto matches=[&](auto& bytes){if(i+4+sizeof(bytes)>code.size())return false;
				for(std::size_t n=0;n<sizeof(bytes);++n)if(code[i+4+n]!=bytes[n])return false;return true;};
			if(matches(complete)){++full;continuation=i+4;}
			if(matches(nonzero))++partial;
		}
		return methods==2 && full==1 && partial==1 ? std::optional{continuation} : std::nullopt;
	}
	inline bool lock_gate_ready(bool near_eye,int loaded,int native_state,int native_time) noexcept
	{
		// Ammo insertion alone may precede native reload completion. Wait for
		// ready/expired weapon time, but not the subsequent visual ADS blend.
		return near_eye && loaded==1 && native_state==0 && native_time<=0;
	}
}
