#pragma once

#include <cstdint>

namespace vr::engine_command_stream
{
	struct snapshot
	{
		std::uintptr_t address{};
		std::uint64_t byte_count{};
		std::uint64_t record_count{};
		std::uint64_t hash{};
		bool valid{};
	};

	[[nodiscard]] snapshot capture(const void* commands) noexcept;
	[[nodiscard]] bool identical(const snapshot& left,
		const snapshot& right) noexcept;
}
