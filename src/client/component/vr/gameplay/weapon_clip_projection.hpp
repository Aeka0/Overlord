#pragma once
#include "weapon_clip_ledger.hpp"
#include "native_ammunition_storage.hpp"
#include "ammunition_transfer.hpp"
#include <optional>

namespace vr::gameplay::weapons::native_ammunition::projection
{
	struct restored_clip {std::uint32_t weapon{};int loaded{},reserve{};std::uint64_t clip_key{},reserve_key{},epoch{};};
	// Stage the complete native ammo projection and physical roster together.
	// A rejected sparse allocation or inconsistent shared pool leaves both intact.
	inline bool restore(clip_ledger& clips,std::span<std::byte> bytes,std::span<const restored_clip> saved,
		std::span<weapon_identity> restored) noexcept
	{
		if(bytes.size()<storage::extent || saved.size()>clip_ledger::capacity || restored.size()!=saved.size())return false;
		auto next=clips;next.clear();std::array<weapon_identity,clip_ledger::capacity> ids{};
		std::array<std::byte,storage::extent> staged{};std::memcpy(staged.data(),bytes.data(),staged.size());
		for(std::size_t i=0;i<saved.size();++i)
		{
			const auto& s=saved[i];
			if(!s.weapon || s.weapon>=512 || s.loaded<0 || s.loaded>1001 || s.reserve<0 || s.reserve>1000000 ||
				(!s.clip_key && (s.loaded || s.reserve || s.reserve_key)) || (s.clip_key && !s.reserve_key))return false;
			for(std::size_t j=0;j<i;++j)
				if((s.reserve_key && saved[j].reserve_key==s.reserve_key && saved[j].reserve!=s.reserve) ||
					(s.clip_key && saved[j].clip_key==s.clip_key && saved[j].weapon!=s.weapon && saved[j].loaded!=s.loaded))return false;
			const bool first=!next.count(s.weapon);ids[i]=next.allocate(s.weapon);
			if(!next.add(ids[i],s.loaded,s.clip_key,s.epoch))return false;
			if(first && s.clip_key)
			{
				const auto before=storage::observe(staged,s.clip_key,s.reserve_key);
				if(!before.valid() || !storage::commit(staged,s.clip_key,s.reserve_key,before.clip.count,before.reserve.count,s.loaded,s.reserve))return false;
			}
		}
		std::memcpy(bytes.data()+0x3f0,staged.data()+0x3f0,storage::extent-0x3f0);
		clips=next;std::copy_n(ids.begin(),saved.size(),restored.begin());return true;
	}
	// Read the actual native call's PS, not the ledger's last server publication.
	// A nonprojected same-model instance must never inherit another gun's debit.
	inline std::optional<ammunition::projection> read_native_boundary(const clip_ledger& clips,std::span<const std::byte> bytes,
		weapon_identity id,std::uint64_t ck,std::uint64_t rk)noexcept
	{
		if(!id.weapon)return std::nullopt;
		if(id.generation)
		{
			const auto* e=clips.find(id);
			if(!e || !e->key || !e->projected || clips.projected(id.weapon)!=id || e->key!=(ck&0xffffffffffull))return std::nullopt;
		}
		const auto native=storage::observe(bytes,ck,rk);
		if(!native.valid())return std::nullopt;
		return ammunition::projection{native.clip.count,native.reserve.count};
	}
	// Pure transaction core, under the adapter's server/identity/epoch admission.
	// All native destinations are preflighted before either ledger or pool changes.
	inline bool commit(clip_ledger& clips,std::span<std::byte> bytes,weapon_identity id,
		std::uint64_t ck,std::uint64_t rk,int expected_clip,int expected_reserve,int loaded,int reserve) noexcept
	{
		if (loaded<0 || loaded>1001) return false;
		const auto native=storage::observe(bytes,ck,rk);
		const auto* e=id.generation ? clips.find(id) : nullptr;
		if (!native.valid() || (id.generation && (!e || !e->key || e->key!=(ck&0xffffffffffull))) || native.reserve.count!=expected_reserve ||
			(e && !e->projected ? e->loaded : native.clip.count)!=expected_clip) return false;
		if (!storage::commit(bytes,ck,rk,native.clip.count,expected_reserve,e && !e->projected ? native.clip.count : loaded,reserve)) return false;
		const auto target=e ? e->id : clips.projected(id.weapon);
		if (const auto* current=clips.find(target)) clips.set(target,current->loaded,loaded);
		return true;
	}
	inline bool select(clip_ledger& clips,std::span<std::byte> bytes,weapon_identity id,std::uint64_t ck,std::uint64_t rk) noexcept
	{
		const auto native=storage::observe(bytes,ck,rk);const auto* next=clips.find(id);
		const auto previous=clips.projected(id.weapon);const auto* old=clips.find(previous);
		if (!native.valid() || !next || !next->key || !old || !old->key) return false;
		if (previous==id) return clips.set(id,next->loaded,native.clip.count);
		if (!storage::commit(bytes,ck,rk,native.clip.count,native.reserve.count,next->loaded,native.reserve.count)) return false;
		clips.set(previous,old->loaded,native.clip.count);return clips.select(id);
	}
	inline bool retire(clip_ledger& clips,std::span<std::byte> bytes,weapon_identity id,std::uint64_t ck,std::uint64_t rk) noexcept
	{
		if (!clips.find(id)) return false;
		auto next=clips;next.erase(id);
		if (const auto* survivor=next.find(next.projected(id.weapon));survivor)
		{
			const auto native=storage::observe(bytes,ck,rk);
			if (!native.valid() || !storage::commit(bytes,ck,rk,native.clip.count,native.reserve.count,survivor->loaded,native.reserve.count)) return false;
		}
		clips=next;return true;
	}
	inline bool begin_pickup(clip_ledger& clips,std::span<std::byte> bytes,std::uint32_t token,std::uint64_t ck,std::uint64_t rk) noexcept
	{
		if (clips.count()>=clip_ledger::capacity) return false;
		const auto native=storage::observe(bytes,ck,rk);
		if (!native.valid() || (!native.clip.at && !native.clip.vacant) || (!native.reserve.at && !native.reserve.vacant)) return false;
		const auto previous=clips.projected(token);const auto* old=clips.find(previous);
		if (!old) return true;
		if (!old->key) return false;
		if (!storage::commit(bytes,ck,rk,native.clip.count,native.reserve.count,0,native.reserve.count)) return false;
		return clips.set(previous,old->loaded,native.clip.count);
	}
	inline bool finish_pickup(clip_ledger& clips,std::span<std::byte> bytes,weapon_identity id,weapon_identity previous,
		std::uint64_t ck,std::uint64_t rk,std::uint64_t epoch,bool accepted) noexcept
	{
		if (!(ck&0xffffffffffull)) return false;
		const auto native=storage::observe(bytes,ck,rk);if (!native.valid()) return false;
		auto next=clips;
		if (accepted && !next.add(id,native.clip.count,ck&0xffffffffffull,epoch)) return false;
		if (const auto* old=clips.find(previous);old)
			if (!storage::commit(bytes,ck,rk,native.clip.count,native.reserve.count,old->loaded,native.reserve.count)) return false;
		if (accepted) clips=next;
		return accepted;
	}
}
