#include <std_include.hpp>

#include "engine_command_stream.hpp"

#include <limits>

namespace vr::engine_command_stream
{
	namespace
	{
		constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
		constexpr std::uint64_t fnv_prime = 1099511628211ull;
		constexpr std::size_t maximum_command_bytes = 16 * 1024 * 1024;
		constexpr std::size_t maximum_command_records = 1'000'000;

		[[nodiscard]] bool readable_protection(const DWORD protection) noexcept
		{
			if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
			switch (protection & 0xFF)
			{
			case PAGE_READONLY:
			case PAGE_READWRITE:
			case PAGE_WRITECOPY:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool readable_range(const std::uintptr_t begin,
			const std::size_t size) noexcept
		{
			if (begin == 0 || size == 0 || begin >
				(std::numeric_limits<std::uintptr_t>::max)() - size)
			{
				return false;
			}
			const auto end = begin + size;
			auto current = begin;
			while (current < end)
			{
				MEMORY_BASIC_INFORMATION information{};
				if (VirtualQuery(reinterpret_cast<const void*>(current), &information,
					sizeof(information)) != sizeof(information) ||
					information.State != MEM_COMMIT ||
					!readable_protection(information.Protect))
				{
					return false;
				}
				const auto region_begin = reinterpret_cast<std::uintptr_t>(
					information.BaseAddress);
				if (region_begin > (std::numeric_limits<std::uintptr_t>::max)() -
					information.RegionSize)
				{
					return false;
				}
				const auto region_end = region_begin + information.RegionSize;
				if (region_end <= current) return false;
				current = (std::min)(region_end, end);
			}
			return true;
		}

		void hash_bytes(std::uint64_t& hash, const void* const data,
			const std::size_t size) noexcept
		{
			const auto* bytes = static_cast<const std::uint8_t*>(data);
			for (std::size_t index{}; index < size; ++index)
			{
				hash ^= bytes[index];
				hash *= fnv_prime;
			}
		}
	}

	snapshot capture(const void* const commands) noexcept
	{
		snapshot result;
		result.address = reinterpret_cast<std::uintptr_t>(commands);
		if (result.address == 0) return result;
		auto cursor = result.address;
		result.hash = fnv_offset;
		while (result.record_count < maximum_command_records &&
			result.byte_count < maximum_command_bytes)
		{
			if (!readable_range(cursor, 3)) return result;
			std::uint16_t record_size{};
			std::uint8_t opcode{};
			std::memcpy(&record_size, reinterpret_cast<const void*>(cursor),
				sizeof(record_size));
			std::memcpy(&opcode, reinterpret_cast<const void*>(cursor + 2),
				sizeof(opcode));
			if (opcode == 0)
			{
				hash_bytes(result.hash, reinterpret_cast<const void*>(cursor), 3);
				result.byte_count += 3;
				result.valid = true;
				return result;
			}
			if (record_size < 4 || record_size > maximum_command_bytes -
				result.byte_count || !readable_range(cursor, record_size))
			{
				return result;
			}
			hash_bytes(result.hash, reinterpret_cast<const void*>(cursor), record_size);
			result.byte_count += record_size;
			++result.record_count;
			cursor += record_size;
		}
		return result;
	}

	bool identical(const snapshot& left, const snapshot& right) noexcept
	{
		return left.valid && right.valid && left.address == right.address &&
			left.byte_count == right.byte_count &&
			left.record_count == right.record_count && left.hash == right.hash;
	}
}
