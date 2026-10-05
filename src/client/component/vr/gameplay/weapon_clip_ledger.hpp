#pragma once
#include "weapon_identity.hpp"
#include <array>
#include <span>
#include <algorithm>

namespace vr::gameplay::weapons
{
	// The native table contains one projection per definition. This server-owned
	// ledger contains physical weapon identities and their optional clips; reserves
	// stay native. Key zero explicitly represents a single weapon without ammunition.
	class clip_ledger
	{
	public:
		static constexpr std::size_t capacity=15;
		struct definition {std::uint32_t weapon{};int loaded{};std::uint64_t key{},epoch{};};
		struct entry {weapon_identity id{};int loaded{};std::uint64_t key{},epoch{};bool projected{};};
		void clear() noexcept {entries_={};} // Never recycle lifetimes after a level reset.
		const auto& entries() const noexcept {return entries_;}
		const entry* find(weapon_identity id) const noexcept
		{for (const auto& e:entries_) if (e.id && e.id==id) return &e;return nullptr;}
		weapon_identity projected(std::uint32_t token) const noexcept
		{for (const auto& e:entries_) if (e.id.weapon==token && e.projected) return e.id;return {};}
		std::size_t count(std::uint32_t token=0) const noexcept
		{return std::count_if(entries_.begin(),entries_.end(),[&](const entry& e){return e.id && (!token || e.id.weapon==token);});}
		weapon_identity allocate(std::uint32_t token) noexcept
		{return token && generation_!=UINT64_MAX ? weapon_identity{token,++generation_} : weapon_identity{};}
		bool add(weapon_identity id,int loaded,std::uint64_t key,std::uint64_t epoch) noexcept
		{
			if (!id || loaded<0 || loaded>1001 || (!key && (loaded || count(id.weapon))) || find(id)) return false;
			for (const auto& e:entries_) if (e.id.weapon==id.weapon && (e.key!=key || e.epoch!=epoch)) return false;
			const bool first=!count(id.weapon);
			for (auto& e:entries_) if (!e.id)
			{e={id,loaded,key,epoch,first};generation_=std::max(generation_,id.generation);return true;}
			return false;
		}
		bool erase(weapon_identity id) noexcept
		{
			for (auto& e:entries_) if (e.id==id && id)
			{
				const bool projection=e.projected;e={};
				if (projection) for (auto& next:entries_) if (next.id.weapon==id.weapon) {next.projected=true;break;}
				return true;
			}
			return false;
		}
		bool set(weapon_identity id,int expected,int loaded) noexcept
		{
			if (loaded<0 || loaded>1001) return false;
			for (auto& e:entries_) if (e.id==id && id && e.loaded==expected && (e.key || !loaded)) {e.loaded=loaded;return true;}
			return false;
		}
		bool select(weapon_identity id) noexcept
		{
			if (!find(id)) return false;
			for (auto& e:entries_) if (e.id.weapon==id.weapon) e.projected=e.id==id;
			return true;
		}
		bool reconcile(std::span<const definition> observed) noexcept
		{
			if (observed.size()>capacity) return false;
			for (std::size_t i=0;i<observed.size();++i)
			{
				const auto& d=observed[i];if (!d.weapon || (!d.key && d.loaded) || d.loaded<0 || d.loaded>1001) return false;
				for (std::size_t j=0;j<i;++j) if (d.weapon==observed[j].weapon) return false;
			}
			auto next=*this;
			for (auto& e:next.entries_) if (e.id && std::none_of(observed.begin(),observed.end(),[&](const definition& d)
				{return d.weapon==e.id.weapon && d.key==e.key && d.epoch==e.epoch;})) e={};
			for (const auto& d:observed)
			{
				const auto id=next.projected(d.weapon);
				if (id) {const auto* e=next.find(id);next.set(id,e->loaded,d.loaded);}
				else if (!next.add(next.allocate(d.weapon),d.loaded,d.key,d.epoch)) return false;
			}
			*this=next;return true;
		}
	private:
		std::array<entry,capacity> entries_{};
		std::uint64_t generation_{};
	};
}
