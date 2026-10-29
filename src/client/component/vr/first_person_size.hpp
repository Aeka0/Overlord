#pragma once
#include <atomic>

namespace vr::first_person_size
{
	// One process-wide size, captured before the first hand solve. Native skin
	// packets and detached-item snapshots retain geometry across render workers;
	// changing size during a live session would mix those coordinate contracts.
	inline std::atomic<float> requested{1.f};
	inline std::atomic<float> applied{};
	inline float value() noexcept
	{
		const auto selected=applied.load(std::memory_order_acquire);
		return selected>0?selected:requested.load(std::memory_order_relaxed);
	}
	inline float freeze() noexcept
	{
		float previous{};
		const auto selected=requested.load(std::memory_order_relaxed);
		return applied.compare_exchange_strong(previous,selected,std::memory_order_acq_rel)?selected:previous;
	}
}
