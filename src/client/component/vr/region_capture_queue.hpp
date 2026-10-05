#pragma once
#include "region_capture.hpp"

namespace vr::region_capture
{
	// Fixed MPSC queue. Contention/full means explicitly counted loss, never
	// waiting on the diagnostics writer from a render/SteamVR thread.
	template<std::size_t Capacity> class queue
	{
		std::atomic_flag gate = ATOMIC_FLAG_INIT;
		std::array<row, Capacity> data{};
		std::size_t head{}, size{};
	public:
		std::atomic_uint64_t dropped{};
		bool push(const row& value) noexcept
		{
			if (gate.test_and_set(std::memory_order_acquire))
			{
				dropped.fetch_add(1, std::memory_order_relaxed);
				return false;
			}
			const bool room = size < Capacity;
			if (room) { data[(head + size) % Capacity] = value; ++size; }
			else dropped.fetch_add(1, std::memory_order_relaxed);
			gate.clear(std::memory_order_release);
			return room;
		}
		std::size_t pop(row* output, std::size_t maximum) noexcept
		{
			if (gate.test_and_set(std::memory_order_acquire)) return 0;
			const auto amount = size < maximum ? size : maximum;
			for (std::size_t i{}; i < amount; ++i) output[i] = data[(head + i) % Capacity];
			head = (head + amount) % Capacity;
			size -= amount;
			gate.clear(std::memory_order_release);
			return amount;
		}
	};
}
