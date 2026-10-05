#pragma once
#include <array>
#include <cstdint>

namespace vr::engine_scene_completion
{
	enum class stage : std::uint8_t { initial, surfaces, effects };
	enum class failure : std::uint8_t { none, identity, incomplete };
	struct flags
	{
		std::uint32_t initial{}, scene{}, surfaces{}, effects{};
		constexpr bool complete() const noexcept { return initial && scene && surfaces && effects; }
	};
	// Shared production/test control flow. Wait MUST be H2's native job-pumping
	// wait, not a timeout, polling loop or new worker. No snapshot may be taken
	// before this returns success; no eye/GPU scope may be held by the caller.
	template<class Identity, class Wait, class ReadFlags>
	failure await_inputs(std::uintptr_t frontend, Identity&& identity, Wait&& wait, ReadFlags&& read_flags)
	{
		if (!frontend || identity() != frontend) return failure::identity;
		for (const auto point : {stage::initial, stage::surfaces, stage::effects})
		{
			wait(point);
			if (identity() != frontend) return failure::identity;
		}
		return read_flags(frontend).complete() ? failure::none : failure::incomplete;
	}
	struct status
	{
		std::uint64_t attempts{}, completions{}, failures{}, pending{};
		failure last_failure{};
	};
	void validate(); // Loader-only native predicate/call ABI validation.
	[[nodiscard]] bool await(std::uintptr_t frontend, const void* record) noexcept;
	[[nodiscard]] status get_status() noexcept;
}
