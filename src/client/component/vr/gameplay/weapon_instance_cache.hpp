#pragma once
#include "weapon_identity.hpp"
#include <array>
#include <cstddef>

namespace vr::gameplay::weapons
{
	// Bounded identity lookup. Never evicts an unrelated live gun on admission;
	// the owner explicitly retires entries when their lifecycle ends.
	// Generation zero is permitted only for the legacy native presentation path.
	template<class T, std::size_t Capacity> class instance_cache
	{
	public:
		struct entry { weapon_identity id{}; T value{}; };
		T* find(weapon_identity id) noexcept
		{ for (auto& v:entries_) if (v.id==id && id.weapon) return &v.value; return nullptr; }
		const T* find(weapon_identity id) const noexcept
		{ for (const auto& v:entries_) if (v.id==id && id.weapon) return &v.value; return nullptr; }
		T* acquire(weapon_identity id) noexcept
		{
			if (!id.weapon) return nullptr;
			if (auto* v=find(id)) return v;
			for (auto& v:entries_) if (!v.id.weapon) { v={id,{}}; return &v.value; }
			return nullptr;
		}
		template<class Keep> void retain(Keep&& keep) noexcept
		{ for (auto& v:entries_) if (v.id.weapon && !keep(v.id)) v={}; }
		auto& entries() noexcept { return entries_; }
		const auto& entries() const noexcept { return entries_; }
	private:
		std::array<entry,Capacity> entries_{};
	};
}
