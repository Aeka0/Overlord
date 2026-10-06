#pragma once
#include "component/d3d11.hpp"

namespace vr::present_transaction
{
	struct key
	{
		bool active{};
		std::uint64_t frame{}, generation{};
		std::uint32_t thread_id{};
	};
	enum class validation
	{
		matched,
		missing_pre,
		frame_mismatch,
		generation_mismatch,
		thread_mismatch,
		present_failed
	};
	inline validation validate(const key& transaction,
	                           const d3d11::present_event& event,
	                           std::uint32_t thread,
	                           HRESULT result) noexcept
	{
		if (!transaction.active)
			return validation::missing_pre;
		if (transaction.frame != event.frame_index)
			return validation::frame_mismatch;
		if (transaction.generation != event.graphics.generation)
			return validation::generation_mismatch;
		if (transaction.thread_id != thread)
			return validation::thread_mismatch;
		if (FAILED(result))
			return validation::present_failed;
		return validation::matched;
	}
	inline bool owner_changed(bool enabled,
	                          bool reinitializing,
	                          std::uint64_t previous_generation,
	                          std::uint32_t previous_thread,
	                          std::uint64_t generation,
	                          std::uint32_t thread) noexcept
	{
		return enabled && !reinitializing && previous_generation == generation && previous_thread != 0 &&
		       previous_thread != thread;
	}
}
