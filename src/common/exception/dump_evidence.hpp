#pragma once

#include <utils/nt.hpp>
#include <DbgHelp.h>
#include <array>
#include <cstdint>

namespace exception::evidence
{
	inline constexpr ULONG stream_type = 0x4F564C44; // OVLD, user-defined stream.
	inline constexpr std::size_t max_ranges = 256;
	inline constexpr std::size_t max_modules = 16;
	inline constexpr std::uint64_t byte_budget = 8 * 1024 * 1024;

	enum class range_kind : std::uint64_t { headers = 1, function_table, unwind_info, code, registers };
	struct memory_range { std::uint64_t address{}, size{}, kind{}; };
	struct module
	{
		std::uint64_t base{}, size{}, timestamp{}, exception_rva{}, exception_size{}, table_scheduled{};
	};
	struct stream
	{
		// Fixed little-endian uint64 fields; see tools/read_crash_evidence.py.
		std::uint64_t version{1}, focus_thread{}, exception_code{}, pc{}, stack_pointer{}, sampled_tick{};
		std::uint64_t range_count{}, module_count{}, candidate_count{}, skipped{}, requested_bytes{};
		std::uint64_t budget{byte_budget}, scanned_stack_bytes{}, context_available{}, stack_candidates_only{1}, limits{};
		std::array<module, max_modules> modules{};
		std::array<memory_range, max_ranges> ranges{};
	};
	static_assert(sizeof(stream) == 128 + max_modules * 48 + max_ranges * 24);

	struct plan
	{
		stream report{};
		std::array<std::uint64_t, 64> candidates{};
		std::size_t next_range{};
	};

	// Bounded CPU reads only: no allocator, loader enumeration, DbgHelp symbol
	// calls, graphics APIs or waiting on a lock held by the faulting thread.
	void prepare(plan& output, const EXCEPTION_POINTERS* exception, DWORD focus_thread) noexcept;
	BOOL CALLBACK callback(void* context, const PMINIDUMP_CALLBACK_INPUT input,
		MINIDUMP_CALLBACK_OUTPUT* output) noexcept;
}
