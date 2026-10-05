#pragma once
#include <cstdint>
#include <optional>

namespace vr::gameplay::weapons::ammunition
{
	// Shared native projection; neither a magazine nor a cylinder owns the
	// meaning of a loaded round in another feed family.
	struct projection
	{
		int loaded{}, reserve{};
		bool operator==(const projection&) const = default;
	};
	// Touch_Item writes min(world clip, base capacity) and separately transfers
	// reserve. Restore only the known +1 truncation of an exact recorded drop;
	// never infer a chamber from a different item or arbitrary changed counts.
	inline std::optional<projection> restore_pickup(projection observed,int saved_loaded,
		int world_loaded,int base_capacity,int maximum_loaded) noexcept
	{
		if (base_capacity<=0 || base_capacity>1000 || maximum_loaded<base_capacity ||
			maximum_loaded>base_capacity+1 || saved_loaded<0 || saved_loaded>maximum_loaded ||
			world_loaded!=saved_loaded || observed.reserve<0 || observed.reserve>1000000) return {};
		if (observed.loaded==saved_loaded) return observed;
		if (saved_loaded==base_capacity+1 && observed.loaded==base_capacity)
			return projection{saved_loaded,observed.reserve};
		return {};
	}
	inline std::optional<projection> release_fault(projection observed,int escrow) noexcept
	{
		if (observed.loaded<0 || observed.loaded>1001 || observed.reserve<0 || escrow<0 ||
			std::int64_t(observed.reserve)+escrow>1000000) return {};
		return projection{observed.loaded,observed.reserve+escrow};
	}
	enum class disposition_reason { deliberate_discard, forced_cleanup, waist_return };
	struct disposition { int returned{}, lost{}; };
	// Waist returns and forced cleanup never count as player discards,
	// including tracking/ownership loss.
	inline disposition dispose(int live, disposition_reason reason, bool penalty = false) noexcept
	{
		if (live < 0) return {};
		return penalty && reason == disposition_reason::deliberate_discard ? disposition{0,live} : disposition{live,0};
	}
	// Shared positive-grant budget reconciliation. Stock pickups may credit the
	// native clip, or clamp a +1 clip to base capacity. They cannot manufacture a
	// physical feed transition; route the same total back into reserve instead.
	inline std::optional<int> granted_reserve(projection before,projection observed,int base_capacity,
		int maximum_loaded,int reserve_limit) noexcept
	{
		if (base_capacity<=0 || maximum_loaded<base_capacity || reserve_limit<0 || before.loaded<0 ||
			before.loaded>maximum_loaded || before.reserve<0 || observed.reserve<before.reserve || observed.reserve>reserve_limit ||
			observed.loaded<0 || observed.loaded>maximum_loaded || observed.loaded==before.loaded ||
			(observed.loaded<before.loaded && observed.loaded!=base_capacity)) return {};
		const auto reserve=std::int64_t(observed.reserve)+observed.loaded-before.loaded;
		if (reserve<0 || reserve>reserve_limit) return {};
		return static_cast<int>(reserve);
	}
	template<class Transaction, class Commit>
	bool commit(Transaction&& transaction, Commit&& write) noexcept(noexcept(write(transaction)))
	{
		return bool(transaction) && write(transaction);
	}
}
