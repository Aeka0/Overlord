#pragma once
#include <span>
#include <cstdint>
#include <cstddef>
#include <array>

namespace vr::gameplay::vehicles
{
	// H2 OP_clearparams (4D) removes values until SCRIPT_CODEPOS (7).
	// A newly called function has SCRIPT_PRECODEPOS (8) under its arguments:
	// only the native parameter prologue may bind them and convert that marker.
	// Keep A8AF's three optional parameter bindings and checkclearparams intact.
	inline constexpr std::array<std::uint8_t,7> target_parameters{0x2c,0,0x2c,1,0x2c,2,0x32};
	inline std::size_t target_body_offset(std::span<const std::uint8_t> code) noexcept
	{
		if(code.size()<target_parameters.size()+5)return 0;
		for(std::size_t i=0;i<target_parameters.size();++i)if(code[i]!=target_parameters[i])return 0;
		return code[7]==0x1a && code[8]==0xa5 && code[9]==1 && code[10]==0x17 && code[11]==3?7:0;
	}
	// Continuation only: the original function has already consumed parameters.
	// Inserting any clear/check-params opcode here would process the wrong frame.
	inline constexpr std::array<char,4> builtin_result_code(std::uint16_t function) noexcept
	{return {char(0x1a),char(function&0xff),char(function>>8),char(0x19)};}
	struct animation_wait {std::size_t begin{},end{};};
	struct animation_waits {std::array<animation_wait,5> sites{};std::size_t count{};bool valid{};};
	// Exact H2 waittillmatch statement: two string operands, vehicle local,
	// match count, wait completion and parameter cleanup. Skip the WHOLE
	// statement so no argument or PRECODEPOS marker is left on the VM stack.
	inline animation_waits inspect_animation_waits(std::span<const std::uint8_t> code,
		std::uint32_t pullout,std::uint32_t putaway,bool snowmobile) noexcept
	{
		animation_waits out;unsigned pulls{},puts{};
		if(!pullout || !putaway || pullout==putaway || code.size()>8192)return out;
		for(std::size_t i=0;i+15<=code.size();++i)
		{
			const auto* p=code.data()+i;if(p[0]!=0x53 || p[5]!=0x53)continue;
			const auto tag=std::uint32_t(p[6])|(std::uint32_t(p[7])<<8)|(std::uint32_t(p[8])<<16)|(std::uint32_t(p[9])<<24);
			if(tag!=pullout && tag!=putaway)continue;
			std::size_t n=10;if(p[n]==0x6f || p[n]==0x73)++n;else if(p[n]==0x75)n+=2;else continue;
			if(i+n+4>code.size() || p[n]!=0xa5 || p[n+1]!=1 || p[n+2]!=0xa4 || p[n+3]!=0x4d)continue;
			if(out.count==out.sites.size())return out;
			out.sites[out.count++]={i,i+n+4};if(tag==pullout)++pulls;else ++puts;
		}
		out.valid=pulls==(snowmobile?2u:1u) && puts==(snowmobile?3u:1u);return out;
	}
	struct script_sites
	{
		std::size_t reload{},idle{},refill{},refill_end{},consume{},consume_end{};
		bool valid{};
	};
	// H2's existing opcode table, with 16-bit canonical field operands. The
	// input is exactly B210's function range, never the complete script asset.
	inline script_sites inspect_script(std::span<const std::uint8_t> code,std::uint16_t field) noexcept
	{
		script_sites out;unsigned reload{},refill{},consume{};
		if(code.size()>8192)return out;
		for(std::size_t i=0;i+14<=code.size();++i)
		{
			const auto* p=code.data()+i;const auto u16=[&](unsigned offset){return unsigned(p[offset])|(unsigned(p[offset+1])<<8);};
			if(p[0]==0x88 && p[2]==0x51 && u16(3)==field && p[5]==0x79 && p[6]==0x38 && p[7]==0xb0)
			{out.reload=i+10;out.idle=i+10+u16(8);++reload;}
			if(p[0]==0xa0 && p[1]==32 && p[2]==0x88 && p[4]==0x52 && u16(5)==field && p[7]==0x5b)
			{out.refill=i;out.refill_end=i+8;++refill;}
			// OP_minus = 0x7b; both references must use the same local vehicle.
			if(p[0]==0x88 && p[2]==0x51 && u16(3)==field && p[5]==0xa0 && p[6]==1 && p[7]==0x7b &&
				p[8]==0x88 && p[9]==p[1] && p[10]==0x52 && u16(11)==field && p[13]==0x5b)
			{out.consume=i;out.consume_end=i+14;++consume;}
		}
		out.valid=reload==1 && refill==1 && consume==1 && out.reload<out.refill && out.refill_end<out.idle && out.idle<code.size() && out.consume_end<=out.reload;
		return out;
	}
}
