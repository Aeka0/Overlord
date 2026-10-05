#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>

namespace utils::native_memory
{
	// Bounded copies of owner-thread native memory. This does not prove asset
	// lifetime, thread ownership or a coherent snapshot; the caller owns those
	// contracts. On failure, discard the destination, which may be partly copied.
	// Keep SEH in this destructor-free leaf and avoid per-field VirtualQuery calls.
	inline bool read_bytes(void* output, const void* source, std::size_t size) noexcept
	{
		const auto address = reinterpret_cast<std::uintptr_t>(source);
		if (!output || address < 0x10000 || !size || size > 0x100000 ||
			address > (std::numeric_limits<std::uintptr_t>::max)()-size) return false;
		__try
		{
			std::memcpy(output, source, size);
			return true;
		}
		__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
			GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
		{
			return false;
		}
	}

	template<class T>
	bool read_at(const void* source, std::size_t offset, T& output) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>);
		const auto address = reinterpret_cast<std::uintptr_t>(source);
		if (!source || offset > (std::numeric_limits<std::uintptr_t>::max)()-address) return false;
		return read_bytes(&output, reinterpret_cast<const void*>(address+offset), sizeof(output));
	}
}
