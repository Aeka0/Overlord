#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr
{
	namespace detail
	{
		template <std::size_t Capacity>
		[[nodiscard]] constexpr std::size_t stereo_pointer_slot(std::uintptr_t value) noexcept
		{
			static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0);
			value >>= 3;
			value ^= value >> 17;
			value *= std::uintptr_t{0x9e3779b97f4a7c15ull};
			value ^= value >> 32;
			return static_cast<std::size_t>(value) & (Capacity - 1);
		}
	}

	// Owner-thread lookup into a separately owned, stable mapping array. Exact
	// keys make collisions misses, never another view's mapping. Clear alongside
	// that array, before releasing its COM identities or reusing its indices.
	template <std::size_t Capacity>
	class stereo_pointer_index
	{
		static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0);
		struct entry { std::uintptr_t identity{}; std::size_t index{}; };
		std::array<entry, Capacity> entries_{};
	public:
		[[nodiscard]] const std::size_t* find(const std::uintptr_t identity) const noexcept
		{
			const auto& value = entries_[detail::stereo_pointer_slot<Capacity>(identity)];
			return identity != 0 && value.identity == identity ? &value.index : nullptr;
		}
		void insert(const std::uintptr_t identity, const std::size_t index) noexcept
		{
			if (identity != 0) entries_[detail::stereo_pointer_slot<Capacity>(identity)] = {identity, index};
		}
		void clear() noexcept { entries_.fill({}); }
	};

	// A bounded, owner-thread negative cache. Collisions evict entries, never
	// produce a positive result for another identity. Reset at the owning pair
	// boundary; a raw COM address must not survive its known lifetime interval.
	template <std::size_t Capacity>
	class stereo_pointer_cache
	{
		static_assert(Capacity != 0 && (Capacity & (Capacity - 1)) == 0);
		static constexpr std::size_t ways = Capacity < 4 ? Capacity : 4;
		static constexpr std::size_t buckets = Capacity / ways;
	public:
		[[nodiscard]] bool contains(const std::uintptr_t identity) const noexcept
		{
			if (identity == 0) return false;
			const auto start = detail::stereo_pointer_slot<buckets>(identity) * ways;
			for (std::size_t i{}; i < ways; ++i)
				if (entries_[start + i] == identity) return true;
			return false;
		}

		void insert(const std::uintptr_t identity) noexcept
		{
			if (identity == 0) return;
			const auto start = detail::stereo_pointer_slot<buckets>(identity) * ways;
			for (std::size_t i{}; i < ways; ++i)
				if (entries_[start + i] == identity) return;
			// Four colliding material views can coexist instead of repeatedly
			// calling GetResource. Storage and lifetime remain bounded per pair.
			for (auto i = ways - 1; i != 0; --i) entries_[start + i] = entries_[start + i - 1];
			entries_[start] = identity;
		}

		void clear() noexcept { entries_.fill(0); }

	private:
		std::array<std::uintptr_t, Capacity> entries_{};
	};
}
