#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace utils::hook_validation
{
	enum class masked_byte_status
	{
		matched,
		invalid_argument,
		address_overflow,
		query_failed,
		uncommitted_memory,
		inaccessible_memory,
		mismatch,
	};

	struct masked_byte_pattern
	{
		const std::uint8_t* bytes{};
		const std::uint8_t* mask{};
		std::size_t size{};
	};

	struct masked_byte_result
	{
		static constexpr std::size_t no_mismatch = (std::numeric_limits<std::size_t>::max)();

		masked_byte_status status{masked_byte_status::invalid_argument};
		std::size_t checked_size{};
		std::size_t mismatch_offset{no_mismatch};
		std::uint8_t expected{};
		std::uint8_t actual{};
		std::uint8_t mask{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return status == masked_byte_status::matched;
		}
	};

	enum class executable_target_status
	{
		executable,
		null_address,
		query_failed,
		uncommitted_memory,
		guarded_memory,
		no_access,
		not_executable,
	};

	struct executable_target_result
	{
		executable_target_status status{executable_target_status::null_address};
		const void* address{};
		const void* allocation_base{};
		const void* region_base{};
		std::size_t region_size{};
		std::uint32_t protection{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return status == executable_target_status::executable;
		}
	};

	[[nodiscard]] masked_byte_result verify_masked_bytes(const void* address,
		const masked_byte_pattern& pattern) noexcept;
	[[nodiscard]] executable_target_result validate_executable_target(const void* address) noexcept;
	[[nodiscard]] const char* to_string(masked_byte_status status) noexcept;
	[[nodiscard]] const char* to_string(executable_target_status status) noexcept;
}
