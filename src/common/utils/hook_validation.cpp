#include "hook_validation.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace utils::hook_validation
{
	namespace
	{
		bool is_readable(const DWORD protection) noexcept
		{
			if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
			{
				return false;
			}

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

		bool is_executable(const DWORD protection) noexcept
		{
			switch (protection & 0xFF)
			{
			case PAGE_EXECUTE:
			case PAGE_EXECUTE_READ:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;
			default:
				return false;
			}
		}
	}

	masked_byte_result verify_masked_bytes(const void* address, const masked_byte_pattern& pattern) noexcept
	{
		masked_byte_result result;
		if (!address || !pattern.bytes || !pattern.mask || pattern.size == 0)
		{
			return result;
		}

		const auto start = reinterpret_cast<std::uintptr_t>(address);
		if (pattern.size - 1 > (std::numeric_limits<std::uintptr_t>::max)() - start)
		{
			result.status = masked_byte_status::address_overflow;
			return result;
		}

		const auto* actual_bytes = static_cast<const std::uint8_t*>(address);
		std::size_t offset = 0;
		while (offset < pattern.size)
		{
			MEMORY_BASIC_INFORMATION memory{};
			if (VirtualQuery(actual_bytes + offset, &memory, sizeof(memory)) != sizeof(memory))
			{
				result.status = masked_byte_status::query_failed;
				result.checked_size = offset;
				return result;
			}

			if (memory.State != MEM_COMMIT)
			{
				result.status = masked_byte_status::uncommitted_memory;
				result.checked_size = offset;
				return result;
			}

			if (!is_readable(memory.Protect))
			{
				result.status = masked_byte_status::inaccessible_memory;
				result.checked_size = offset;
				return result;
			}

			const auto region_start = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
			if (memory.RegionSize > (std::numeric_limits<std::uintptr_t>::max)() - region_start)
			{
				result.status = masked_byte_status::address_overflow;
				result.checked_size = offset;
				return result;
			}

			const auto region_end = region_start + memory.RegionSize;
			const auto current = start + offset;
			if (current < region_start || current >= region_end)
			{
				result.status = masked_byte_status::address_overflow;
				result.checked_size = offset;
				return result;
			}

			const auto available = region_end - current;
			const auto remaining = pattern.size - offset;
			const auto chunk_size = static_cast<std::size_t>(available < remaining ? available : remaining);

			for (std::size_t i = 0; i < chunk_size; ++i)
			{
				const auto index = offset + i;
				const auto mask = pattern.mask[index];
				const auto expected = static_cast<std::uint8_t>(pattern.bytes[index] & mask);
				const auto actual = static_cast<std::uint8_t>(actual_bytes[index] & mask);
				if (actual != expected)
				{
					result.status = masked_byte_status::mismatch;
					result.checked_size = index;
					result.mismatch_offset = index;
					result.expected = pattern.bytes[index];
					result.actual = actual_bytes[index];
					result.mask = mask;
					return result;
				}
			}

			offset += chunk_size;
		}

		result.status = masked_byte_status::matched;
		result.checked_size = pattern.size;
		return result;
	}

	executable_target_result validate_executable_target(const void* address) noexcept
	{
		executable_target_result result;
		result.address = address;
		if (!address)
		{
			return result;
		}

		MEMORY_BASIC_INFORMATION memory{};
		if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory))
		{
			result.status = executable_target_status::query_failed;
			return result;
		}

		result.allocation_base = memory.AllocationBase;
		result.region_base = memory.BaseAddress;
		result.region_size = memory.RegionSize;
		result.protection = memory.Protect;

		if (memory.State != MEM_COMMIT)
		{
			result.status = executable_target_status::uncommitted_memory;
		}
		else if ((memory.Protect & PAGE_GUARD) != 0)
		{
			result.status = executable_target_status::guarded_memory;
		}
		else if ((memory.Protect & PAGE_NOACCESS) != 0)
		{
			result.status = executable_target_status::no_access;
		}
		else if (!is_executable(memory.Protect))
		{
			result.status = executable_target_status::not_executable;
		}
		else
		{
			result.status = executable_target_status::executable;
		}

		return result;
	}

	const char* to_string(const masked_byte_status status) noexcept
	{
		switch (status)
		{
		case masked_byte_status::matched: return "matched";
		case masked_byte_status::invalid_argument: return "invalid argument";
		case masked_byte_status::address_overflow: return "address overflow";
		case masked_byte_status::query_failed: return "memory query failed";
		case masked_byte_status::uncommitted_memory: return "uncommitted memory";
		case masked_byte_status::inaccessible_memory: return "inaccessible memory";
		case masked_byte_status::mismatch: return "byte mismatch";
		default: return "unknown";
		}
	}

	const char* to_string(const executable_target_status status) noexcept
	{
		switch (status)
		{
		case executable_target_status::executable: return "executable";
		case executable_target_status::null_address: return "null address";
		case executable_target_status::query_failed: return "memory query failed";
		case executable_target_status::uncommitted_memory: return "uncommitted memory";
		case executable_target_status::guarded_memory: return "guarded memory";
		case executable_target_status::no_access: return "no access";
		case executable_target_status::not_executable: return "not executable";
		default: return "unknown";
		}
	}
}
