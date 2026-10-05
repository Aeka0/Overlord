#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <array>

namespace vr::gameplay::weapons::native_ammunition::storage
{
	// H2 sparse stores: an absent identity reads as zero (BG_GetAmmoInClip),
	// not an invalid weapon. Identity padding is never compared. Writes require
	// the caller's owning server scope and already validated inventory token.
	inline constexpr std::size_t extent=0x4a4+15*24;
	struct cell { std::size_t at{}, vacant{}; int count{}; bool valid{}; };
	struct view { cell clip{}, reserve{}; bool valid() const noexcept { return clip.valid && reserve.valid; } };
	template<class T> T read(std::span<const std::byte> bytes,std::size_t at) noexcept
	{ T value{}; std::memcpy(&value,bytes.data()+at,sizeof(value)); return value; }
	inline cell find(std::span<const std::byte> bytes,std::uint64_t key,bool clip) noexcept
	{
		cell out;
		const auto mask=clip ? 0xffffffffffull : 0xffffffffffffull;
		key &= mask;
		if (bytes.size()<extent || !key) return out;
		int matches{};
		for (std::size_t i=0;i<15;++i)
		{
			const auto at=clip ? 0x4a4+i*24 : 0x3f0+i*12;
			const auto identity=read<std::uint64_t>(bytes,at)&mask;
			if (identity==key) { ++matches; out.at=at; out.count=read<int>(bytes,at+8); }
			else if (!identity && !out.vacant) out.vacant=at;
		}
		out.valid=matches<=1 && out.count>=0 && out.count<=(clip ? 1001 : 1000000);
		return out;
	}
	inline view observe(std::span<const std::byte> bytes,std::uint64_t clip,std::uint64_t reserve) noexcept
	{ return {find(bytes,clip,true),find(bytes,reserve,false)}; }
	struct reserve_change {std::uint64_t key{};int before{},after{};};
	// Two independent supplies exchange escrow without touching either clip.
	// Validate both counts and any sparse allocations before the first write.
	inline bool commit_reserve_pair(std::span<std::byte> bytes,std::array<reserve_change,2> changes)noexcept
	{
		for(auto& c:changes)c.key&=0xffffffffffffull;
		if(!changes[0].key || !changes[1].key || changes[0].key==changes[1].key)return false;
		std::array<cell,2> cells{};
		for(size_t n=0;n<2;++n)
		{
			const auto& c=changes[n];auto& target=cells[n];target=find(bytes,c.key,false);
			if(!target.valid || target.count!=c.before || c.after<0 || c.after>1000000)return false;
			if(!target.at && c.after)
			{
				for(size_t i=0;i<15;++i)
				{
					const auto at=0x3f0+i*12;
					if(!(read<std::uint64_t>(bytes,at)&0xffffffffffffull) && (!n || cells[0].at!=at)){target.at=at;break;}
				}
				if(!target.at)return false;
			}
		}
		for(size_t n=0;n<2;++n)if(cells[n].at)
		{
			const auto at=cells[n].at;const auto& c=changes[n];
			if(!(read<std::uint64_t>(bytes,at)&0xffffffffffffull))
			{std::memset(bytes.data()+at,0,12);std::memcpy(bytes.data()+at,&c.key,sizeof(c.key));}
			std::memcpy(bytes.data()+at+8,&c.after,sizeof(c.after));
		}
		return true;
	}
	inline bool commit(std::span<std::byte> bytes,std::uint64_t clip_key,std::uint64_t reserve_key,
		int expected_clip,int expected_reserve,int loaded,int reserve) noexcept
	{
		if (loaded<0 || loaded>1001 || reserve<0 || reserve>1000000) return false;
		const auto before=observe(bytes,clip_key,reserve_key);
		if (!before.valid() || before.clip.count!=expected_clip || before.reserve.count!=expected_reserve) return false;
		// Preflight BOTH stores before changing either. Unlike the native clip
		// allocator's full-table fallback, never overwrite an unrelated first cell.
		if ((!before.clip.at && loaded && !before.clip.vacant) ||
			(!before.reserve.at && reserve && !before.reserve.vacant)) return false;
		const auto write=[&](cell target,std::uint64_t key,int count,bool clip) {
			if (!target.at)
			{
				if (!count) return; // Keep zero sparse; observation never allocates.
				target.at=target.vacant;
				std::memset(bytes.data()+target.at,0,clip ? 24 : 12);
				key &= clip ? 0xffffffffffull : 0xffffffffffffull;
				std::memcpy(bytes.data()+target.at,&key,sizeof(key));
			}
			std::memcpy(bytes.data()+target.at+8,&count,sizeof(count));
		};
		write(before.clip,clip_key,loaded,true);
		write(before.reserve,reserve_key,reserve,false);
		return true;
	}
}
