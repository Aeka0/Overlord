#include <std_include.hpp>

#include "engine_stereo_dynamic_arena.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <psapi.h>

namespace vr::engine_stereo_dynamic_arena
{
	namespace
	{
#ifdef H2VR_DYNAMIC_ARENA_TESTING
		std::uint64_t validation_query_count{};
		std::uint64_t resident_validation_count{};
#endif
		// Exists for one synchronous capture/replace/restore call only. Native
		// rendering never runs while it is retained; each next boundary re-queries
		// protection. Actual reads/writes remain guarded against concurrent faults.
		struct range_validation
		{
			struct region { std::uintptr_t begin{}, end{}; bool writable{}; };
			std::array<region, 4> regions{};
			std::size_t count{};

			std::uintptr_t checked_end(std::uintptr_t address, bool writable) const noexcept
			{
				for (std::size_t i{}; i < count; ++i)
					if (address >= regions[i].begin && address < regions[i].end &&
						(!writable || regions[i].writable)) return regions[i].end;
				return 0;
			}
		};

		// Query only the pages we actually touch. VirtualQuery also discovers the
		// end of an entire equal-protection region (large native image arenas are
		// expensive). Invalid/nonresident results retain the original fallback.
		// This proof still expires at the end of each synchronous operation.
		enum class page_result { unknown, accepted, rejected };
		page_result resident_range(std::uintptr_t address, std::size_t size,
			bool writable, range_validation& validation) noexcept
		{
			static const auto page_size = []
			{
				SYSTEM_INFO info{};
				GetSystemInfo(&info);
				return std::uintptr_t{info.dwPageSize};
			}();
			const auto first = address - address % page_size;
			const auto last = (address + size - 1) - (address + size - 1) % page_size;
			if ((last - first) / page_size >= 2 ||
				last > (std::numeric_limits<std::uintptr_t>::max)() - page_size)
				return page_result::unknown;
			std::array<PSAPI_WORKING_SET_EX_INFORMATION, 2> pages{};
			const auto count = static_cast<std::size_t>((last - first) / page_size + 1);
			for (std::size_t i{}; i < count; ++i)
				pages[i].VirtualAddress = reinterpret_cast<void*>(first + i * page_size);
#ifdef H2VR_DYNAMIC_ARENA_TESTING
			++validation_query_count;
#endif
			if (!K32QueryWorkingSetEx(GetCurrentProcess(), pages.data(),
				static_cast<DWORD>(count * sizeof(pages[0])))) return page_result::unknown;
			for (std::size_t i{}; i < count; ++i)
				if (!pages[i].VirtualAttributes.Valid) return page_result::unknown;
			for (std::size_t i{}; i < count; ++i)
			{
				const auto flags = pages[i].VirtualAttributes.Win32Protection;
				const auto protection = flags & 0xFFu;
				const bool can_write = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
					protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
				const bool can_read = can_write || protection == PAGE_READONLY || protection == PAGE_EXECUTE_READ;
				if (pages[i].VirtualAttributes.Bad || (flags & (PAGE_GUARD | PAGE_NOACCESS)) ||
					!can_read || (writable && !can_write)) return page_result::rejected;
				if (validation.count < validation.regions.size())
					validation.regions[validation.count++] =
						{first + i * page_size, first + (i + 1) * page_size, can_write};
			}
#ifdef H2VR_DYNAMIC_ARENA_TESTING
			++resident_validation_count;
#endif
			return page_result::accepted;
		}

		[[nodiscard]] bool checked_add(const std::uintptr_t base,
			const std::uintptr_t offset, std::uintptr_t& output) noexcept
		{
			if (base == 0 || base >
				(std::numeric_limits<std::uintptr_t>::max)() - offset)
			{
				return false;
			}
			output = base + offset;
			return true;
		}

		[[nodiscard]] bool committed_range(const std::uintptr_t address,
			const std::size_t size, const bool writable, range_validation& validation) noexcept
		{
			const auto maximum = (std::numeric_limits<std::uintptr_t>::max)();
			if (address == 0 || size == 0 || address > maximum - size) return false;
			const auto end = address + size;
			if (validation.checked_end(address, writable) >= end) return true;
			const auto resident = resident_range(address, size, writable, validation);
			if (resident != page_result::unknown) return resident == page_result::accepted;
			auto cursor = address;
			while (cursor < end)
			{
				if (const auto checked_end = validation.checked_end(cursor, writable))
				{
					cursor = (std::min)(end, checked_end);
					continue;
				}
				MEMORY_BASIC_INFORMATION information{};
#ifdef H2VR_DYNAMIC_ARENA_TESTING
				++validation_query_count;
#endif
				if (VirtualQuery(reinterpret_cast<const void*>(cursor), &information,
					sizeof(information)) != sizeof(information) ||
					information.State != MEM_COMMIT ||
					(information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
				{
					return false;
				}
				const auto protection = information.Protect & 0xFFu;
				const auto readable = protection == PAGE_READONLY ||
					protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
					protection == PAGE_EXECUTE_READ ||
					protection == PAGE_EXECUTE_READWRITE ||
					protection == PAGE_EXECUTE_WRITECOPY;
				const auto writeable = protection == PAGE_READWRITE ||
					protection == PAGE_WRITECOPY ||
					protection == PAGE_EXECUTE_READWRITE ||
					protection == PAGE_EXECUTE_WRITECOPY;
				if (!readable || (writable && !writeable)) return false;
				const auto region = reinterpret_cast<std::uintptr_t>(
					information.BaseAddress);
				if (region > maximum - information.RegionSize) return false;
				const auto region_end = region + information.RegionSize;
				if (region_end <= cursor) return false;
				if (validation.count < validation.regions.size())
					validation.regions[validation.count++] = {region, region_end, writeable};
				cursor = (std::min)(end, region_end);
			}
			return true;
		}

		[[nodiscard]] bool read_value(const std::uintptr_t address,
			std::uintptr_t& output, range_validation& validation) noexcept
		{
			if (!committed_range(address, sizeof(output), false, validation)) return false;
			__try
			{
				std::memcpy(&output, reinterpret_cast<const void*>(address),
					sizeof(output));
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		[[nodiscard]] bool descriptor_field_address(const std::uintptr_t data,
			const std::size_t index, const std::uintptr_t field_offset,
			std::uintptr_t& output) noexcept
		{
			std::uintptr_t descriptor{};
			return index < mesh_count && checked_add(data, mesh_offset(index), descriptor) &&
				checked_add(descriptor, field_offset, output);
		}

		[[nodiscard]] bool descriptor_field_span(const std::uintptr_t data,
			const std::uintptr_t field_offset,
			std::uintptr_t& begin, std::size_t& size) noexcept
		{
			std::uintptr_t last{};
			if (!descriptor_field_address(data, 0, field_offset, begin) ||
				!descriptor_field_address(data, mesh_count - 1, field_offset, last) ||
				last < begin ||
				last > (std::numeric_limits<std::uintptr_t>::max)() -
					sizeof(std::uintptr_t))
			{
				return false;
			}
			size = static_cast<std::size_t>(last - begin) + sizeof(std::uintptr_t);
			return true;
		}

		[[nodiscard]] bool descriptor_arena_span(const std::uintptr_t data,
			std::uintptr_t& begin, std::size_t& size) noexcept
		{
			constexpr auto first_field = index_count_offset;
			constexpr auto last_field = vertex_buffer_pointer_offset;
			std::uintptr_t last{};
			if (!descriptor_field_address(data, 0, first_field, begin) ||
				!descriptor_field_address(data, mesh_count - 1, last_field, last) ||
				last < begin || last > (std::numeric_limits<std::uintptr_t>::max)() -
					sizeof(std::uintptr_t))
			{
				return false;
			}
			size = static_cast<std::size_t>(last - begin) + sizeof(std::uintptr_t);
			return true;
		}

		// Keep SEH in destructor-free leaves. capture_data validates the complete
		// eight-descriptor arena once, so individual fields must not issue another
		// VirtualQuery on this per-view-copy hot path.
		[[nodiscard]] bool read_descriptor_field_unchecked(const std::uintptr_t data,
			const std::uintptr_t field_offset,
			std::array<std::uintptr_t, mesh_count>& output) noexcept
		{
			__try
			{
				for (std::size_t index{}; index < output.size(); ++index)
				{
					std::uintptr_t address{};
					if (!descriptor_field_address(data, index, field_offset, address))
						return false;
					std::memcpy(&output[index], reinterpret_cast<const void*>(address),
						sizeof(output[index]));
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		[[nodiscard]] bool read_descriptor_u32_field_unchecked(const std::uintptr_t data,
			const std::uintptr_t field_offset,
			std::array<std::uint32_t, mesh_count>& output) noexcept
		{
			__try
			{
				for (std::size_t index{}; index < output.size(); ++index)
				{
					std::uintptr_t address{};
					if (!descriptor_field_address(data, index, field_offset, address))
						return false;
					std::memcpy(&output[index], reinterpret_cast<const void*>(address),
						sizeof(output[index]));
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		[[nodiscard]] bool write_index_bases_unchecked(const std::uintptr_t data,
			const std::array<std::uintptr_t, mesh_count>& values) noexcept
		{
			__try
			{
				for (std::size_t index{}; index < values.size(); ++index)
				{
					std::uintptr_t address{};
					if (!descriptor_field_address(data, index,
						index_base_pointer_offset, address)) return false;
					std::memcpy(reinterpret_cast<void*>(address), &values[index],
						sizeof(values[index]));
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		[[nodiscard]] bool capture_data(const std::uintptr_t data,
			index_base_snapshot& output, failure& error, range_validation& validation) noexcept
		{
			output = {};
			if (data == 0)
			{
				error = failure::data_identity;
				return false;
			}
			output.data_identity = data;
			std::uintptr_t arena_begin{};
			std::size_t arena_size{};
			if (!descriptor_arena_span(data, arena_begin, arena_size) ||
				!committed_range(arena_begin, arena_size, false, validation))
			{
				output = {};
				error = failure::descriptor_unreadable;
				return false;
			}
			std::array<std::uint32_t, mesh_count> index_counts{};
			if (!read_descriptor_u32_field_unchecked(data, index_count_offset,
					index_counts) ||
				!read_descriptor_field_unchecked(data, index_buffer_pointer_offset,
				output.index_buffers) ||
				!read_descriptor_field_unchecked(data, index_base_pointer_offset,
					output.index_bases) ||
				!read_descriptor_u32_field_unchecked(data, vertex_payload_bytes_offset,
					output.vertex_payload_bytes) ||
				!read_descriptor_field_unchecked(data, vertex_buffer_pointer_offset,
					output.vertex_buffers))
			{
				output = {};
				error = failure::descriptor_unreadable;
				return false;
			}
			for (std::size_t index{}; index < index_counts.size(); ++index)
			{
				if (index_counts[index] >
					(std::numeric_limits<std::uint32_t>::max)() / 2u)
				{
					output = {};
					error = failure::descriptor_unreadable;
					return false;
				}
				output.index_payload_bytes[index] = index_counts[index] * 2u;
			}
			output.valid = true;
			error = failure::none;
			return true;
		}

		[[nodiscard]] bool write_snapshot(const index_base_snapshot& value,
			failure& error, range_validation& validation) noexcept
		{
			if (!value.valid || value.data_identity == 0)
			{
				error = failure::invalid_argument;
				return false;
			}
			std::uintptr_t begin{};
			std::size_t size{};
			if (!descriptor_field_span(value.data_identity,
				index_base_pointer_offset, begin, size) ||
				!committed_range(begin, size, true, validation))
			{
				error = failure::descriptor_unwritable;
				return false;
			}
			if (!write_index_bases_unchecked(value.data_identity, value.index_bases))
			{
				error = failure::write;
				return false;
			}
			std::array<std::uintptr_t, mesh_count> verification{};
			if (!read_descriptor_field_unchecked(value.data_identity,
					index_base_pointer_offset, verification) ||
				verification != value.index_bases)
			{
				error = failure::verification;
				return false;
			}
			error = failure::none;
			return true;
		}
	}

	bool capture_frontend_data(const std::uintptr_t frontend_data,
		index_base_snapshot& output, failure& error) noexcept
	{
		range_validation validation;
		return capture_data(frontend_data, output, error, validation);
	}

	namespace
	{
		bool capture_backend_checked(void* const backend_state, index_base_snapshot& output,
			failure& error, range_validation& validation) noexcept
		{
			output = {};
			if (backend_state == nullptr)
			{
				error = failure::invalid_argument;
				return false;
			}
			std::uintptr_t pointer_slot{};
			if (!checked_add(reinterpret_cast<std::uintptr_t>(backend_state),
				backend_data_pointer_offset, pointer_slot))
			{
				error = failure::backend_unreadable;
				return false;
			}
			std::uintptr_t data{};
			if (!read_value(pointer_slot, data, validation))
			{
				error = failure::backend_unreadable;
				return false;
			}
			return capture_data(data, output, error, validation);
		}
	}

	bool capture_backend(void* const backend_state, index_base_snapshot& output,
		failure& error) noexcept
	{
		range_validation validation;
		return capture_backend_checked(backend_state, output, error, validation);
	}

	bool validate_backend(void* const backend_state,
		const index_base_snapshot& expected, failure& error) noexcept
	{
		index_base_snapshot observed{};
		if (!capture_backend(backend_state, observed, error)) return false;
		if (!expected.valid || observed.data_identity != expected.data_identity)
		{
			error = failure::data_identity;
			return false;
		}
		if (observed.index_bases != expected.index_bases)
		{
			error = failure::verification;
			return false;
		}
		error = failure::none;
		return true;
	}

	bool replace(const index_base_snapshot& desired,
		index_base_snapshot& displaced, failure& error) noexcept
	{
		range_validation validation;
		displaced = {};
		if (!desired.valid ||
			!capture_data(desired.data_identity, displaced, error, validation)) return false;
		if (write_snapshot(desired, error, validation)) return true;
		const auto replace_error = error;
		failure ignored{};
		if (write_snapshot(displaced, ignored, validation))
		{
			displaced = {};
			error = replace_error;
		}
		else
		{
			error = ignored;
		}
		return false;
	}

	bool replace_backend(void* const backend_state,
		const index_base_snapshot& desired, index_base_snapshot& displaced,
		failure& error) noexcept
	{
		range_validation validation;
		displaced = {};
		if (!desired.valid ||
			!capture_backend_checked(backend_state, displaced, error, validation)) return false;
		if (displaced.data_identity != desired.data_identity)
		{
			displaced = {};
			error = failure::data_identity;
			return false;
		}
		if (write_snapshot(desired, error, validation)) return true;
		const auto replace_error = error;
		failure ignored{};
		if (write_snapshot(displaced, ignored, validation))
		{
			displaced = {};
			error = replace_error;
		}
		else
		{
			error = ignored;
		}
		return false;
	}

	bool restore(const index_base_snapshot& saved, failure& error) noexcept
	{
		range_validation validation;
		return write_snapshot(saved, error, validation);
	}

#ifdef H2VR_DYNAMIC_ARENA_TESTING
	std::uint64_t validation_queries_for_tests() noexcept { return validation_query_count; }
	std::uint64_t resident_validations_for_tests() noexcept { return resident_validation_count; }
#endif

	const char* to_string(const failure value) noexcept
	{
		switch (value)
		{
		case failure::none: return "none";
		case failure::invalid_argument: return "invalid_argument";
		case failure::backend_unreadable: return "backend_unreadable";
		case failure::data_identity: return "data_identity";
		case failure::descriptor_unreadable: return "descriptor_unreadable";
		case failure::descriptor_unwritable: return "descriptor_unwritable";
		case failure::write: return "write";
		case failure::verification: return "verification";
		case failure::boundary_capacity: return "boundary_capacity";
		case failure::boundary_order: return "boundary_order";
		case failure::boundary_count: return "boundary_count";
		default: return "unknown";
		}
	}
}
