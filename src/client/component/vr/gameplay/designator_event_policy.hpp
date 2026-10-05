#pragma once
#include <span>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <optional>
#include <array>
namespace vr::gameplay::equipment::special::designator_events
{
	enum class activation_phase {unknown,idle,requested,opening,closing};
	struct activation_state
	{
		bool device{},enabled{},active{},selected{},locked{};
		activation_phase phase{};
		bool ready()const noexcept{return device && enabled && active && selected && !locked && phase!=activation_phase::closing;}
		bool orphaned(bool native_empty,bool reserved)const noexcept{return device && enabled && active && locked && !selected && native_empty && !reserved && phase==activation_phase::opening;}
	};
	struct transition_sites {std::size_t idle{},opening{},closing{},pacing{};std::array<std::size_t,2> switches{};};
	inline std::optional<std::size_t> pickup_lock(std::span<const std::byte> code)noexcept
	{
		constexpr std::array<unsigned char,5> pattern{0x70,0xa9,0xd5,0x82,0x6a};
		if(code.size()>8192)return {};std::optional<std::size_t> found;
		for(std::size_t i=0;i+pattern.size()<=code.size();++i)if(!std::memcmp(code.data()+i,pattern.data(),pattern.size())){if(found)return {};found=i;}
		return found;
	}
	inline std::optional<transition_sites> transition_layout(std::span<const std::byte> code,std::uint32_t device,std::uint32_t use)noexcept
	{
		if(code.size()>8192)return {};transition_sites out;unsigned idle{},opening{},closing{},switches{};
		for(std::size_t i=0;i<code.size();++i)
		{
			if(i+4<=code.size() && code[i]==std::byte{0xaa} && code[i+1]==std::byte{0x20} && code[i+2]==std::byte{0x83} && code[i+3]==std::byte{0x6a})
			{if(switches==2)return {};out.switches[switches++]=i;}
			if(i+8<=code.size() && code[i]==std::byte{0x53} && code[i+5]==std::byte{0x70} && code[i+6]==std::byte{0x86} && code[i+7]==std::byte{0x4d})
			{std::uint32_t name{};std::memcpy(&name,code.data()+i+1,4);if(name==use){out.idle=i;++idle;}}
			constexpr std::array<unsigned char,5> prefix{0x70,0xa9,0x1c,0x83,0x53};
			constexpr std::array<unsigned char,7> tail{0xb0,4,0,0x64,0x92,0x11,0};
			if(i+17>code.size() || std::memcmp(code.data()+i,prefix.data(),5) || std::memcmp(code.data()+i+10,tail.data(),7))continue;
			std::uint32_t name{};std::memcpy(&name,code.data()+i+5,4);if(name!=device)continue;
			if(code[i+9]==std::byte{0x90}){out.closing=i;++closing;}else if(code[i+9]==std::byte{0x28}){out.opening=i;++opening;}
		}
		if(idle!=1 || opening!=1 || closing!=1 || switches!=2 || !(out.switches[0]<out.closing && out.closing<out.switches[1] && out.switches[1]<out.opening))return {};
		unsigned pacing{};
		constexpr std::array<unsigned char,7> pause{0x2b,0xcd,0xcc,0x4c,0x3d,0x7a,0x92};
		for(std::size_t i=0;i+9<=code.size();++i)if(!std::memcmp(code.data()+i,pause.data(),pause.size()))
		{std::uint16_t back{};std::memcpy(&back,code.data()+i+7,2);if(i+9>=back && i+9-back==out.idle){out.pacing=i;++pacing;}}
		return pacing==1?std::optional<transition_sites>{out}:std::nullopt;
	}
	inline std::optional<std::size_t> startup_wait(std::span<const std::byte> code)noexcept
	{
		// Stack-neutral native animation waits followed by enableweaponswitch.
		constexpr std::array<unsigned char,26> pattern{0x2b,0xcd,0xcc,0xcc,0x3d,0x7a,0x70,0xa9,0x9b,0x85,0x3f,0xe8,0x03,0x3d,
			0x2b,0xcd,0xcc,0xcc,0x3d,0x7b,0x7a,0x70,0xa9,0x2d,0x83,0x6a};
		if(code.size()>8192)return {};std::optional<std::size_t> found;
		for(std::size_t i=0;i+pattern.size()<=code.size();++i)if(!std::memcmp(code.data()+i,pattern.data(),pattern.size())){if(found)return {};found=i;}
		return found;
	}
	inline bool forward_confirmation(bool vr,std::string_view weapon)noexcept
	{return !vr || weapon=="usp_laserdesignator";}
	struct cooldown
	{
		int accepted_at{};bool used{};
		bool ready(int now)const noexcept{return !used || now<accepted_at || std::int64_t(now)-accepted_at>=3000;}
		bool accept(int now)noexcept{if(!ready(now))return false;accepted_at=now;used=true;return true;}
	};
	inline std::optional<std::size_t> notify_operand(std::span<const std::byte> code,std::uint32_t event)noexcept
	{
		if(code.size()<8 || code.size()>8192)return {};std::optional<std::size_t> found;
		for(std::size_t i=0;i+8<=code.size();++i){std::uint32_t value{};
			if(code[i]!=std::byte{0x53} || code[i+5]!=std::byte{0x41} || code[i+6]!=std::byte{0x69} || code[i+7]!=std::byte{0x34})continue;
			std::memcpy(&value,code.data()+i+1,4);if(value!=event)continue;if(found)return {};found=i+1;}
		return found;
	}
	// Captured H2 GetString(u32), GetSelf, waittill, clearparams. Change only
	// the event operand of one uniquely validated waiter inside one function.
	inline std::optional<std::size_t> wait_operand(std::span<const std::byte> code,std::uint32_t event)noexcept
	{
		if(code.size()<8 || code.size()>8192)return {};
		std::optional<std::size_t> found;
		for(std::size_t i=0;i+8<=code.size();++i)
		{
			if(code[i]!=std::byte{0x53} || code[i+5]!=std::byte{0x41} || code[i+6]!=std::byte{0x86} || code[i+7]!=std::byte{0x4d})continue;
			std::uint32_t value{};std::memcpy(&value,code.data()+i+1,4);if(value!=event)continue;
			if(found)return {};found=i+1;
		}
		return found;
	}
}
