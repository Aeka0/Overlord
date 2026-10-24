#include "dump_evidence.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace exception::evidence
{
	namespace
	{
		constexpr std::uint64_t max_table_bytes = 2 * 1024 * 1024;

		bool read(const std::uint64_t address, void* destination, const std::size_t size) noexcept
		{
			SIZE_T received{};
			return address && size <= (std::numeric_limits<std::uint64_t>::max)() - address &&
				ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), destination, size, &received) &&
				received == size;
		}

		bool readable(const DWORD protection) noexcept
		{
			if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
			const auto flags = protection & 0xff;
			return flags == PAGE_READONLY || flags == PAGE_READWRITE || flags == PAGE_WRITECOPY ||
				flags == PAGE_EXECUTE_READ || flags == PAGE_EXECUTE_READWRITE || flags == PAGE_EXECUTE_WRITECOPY;
		}

		bool add_range(plan& state, const std::uint64_t address, const std::uint64_t size, const range_kind kind) noexcept
		{
			auto& report = state.report;
			if (!address || !size || size > (std::numeric_limits<std::uint64_t>::max)() - address)
			{
				++report.skipped;
				return false;
			}
			for (std::size_t i{}; i < report.range_count; ++i)
			{
				const auto& previous = report.ranges[i];
				if (address >= previous.address && address - previous.address <= previous.size &&
					size <= previous.size - (address - previous.address)) return true;
			}
			if (size > report.budget - report.requested_bytes || report.range_count == max_ranges)
			{
				report.limits |= size > report.budget - report.requested_bytes ? 2 : 1;
				++report.skipped;
				return false;
			}
			// Do not schedule guarded, reserved or partly inaccessible spans. A
			// later unmap can still race the dump; readers verify actual coverage.
			for (auto cursor = address; cursor < address + size;)
			{
				MEMORY_BASIC_INFORMATION region{};
				if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &region, sizeof(region)) ||
					region.State != MEM_COMMIT || !readable(region.Protect))
				{
					++report.skipped;
					return false;
				}
				const auto available = region.RegionSize - (cursor - reinterpret_cast<std::uint64_t>(region.BaseAddress));
				if (!available) { ++report.skipped; return false; }
				cursor += (std::min)(available, address + size - cursor);
			}
			report.ranges[report.range_count++] = {address, size, static_cast<std::uint64_t>(kind)};
			report.requested_bytes += size;
			return true;
		}

		module* find_module(plan& state, const std::uint64_t base) noexcept
		{
			auto& report = state.report;
			for (std::size_t i{}; i < report.module_count; ++i)
				if (report.modules[i].base == base) return &report.modules[i];
			if (report.module_count == max_modules) { ++report.skipped; report.limits |= 4; return nullptr; }
			IMAGE_DOS_HEADER dos{};
			IMAGE_NT_HEADERS64 headers{};
			if (!read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
				dos.e_lfanew < 0 || dos.e_lfanew > 0x10000 ||
				!read(base + dos.e_lfanew, &headers, sizeof(headers)) || headers.Signature != IMAGE_NT_SIGNATURE ||
				headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
				headers.OptionalHeader.SizeOfImage < 0x1000 ||
				headers.OptionalHeader.SizeOfImage > 0x80000000u ||
				headers.OptionalHeader.SizeOfImage > (std::numeric_limits<std::uint64_t>::max)() - base) return nullptr;
			auto& result = report.modules[report.module_count++];
			result.base = base;
			result.size = headers.OptionalHeader.SizeOfImage;
			result.timestamp = headers.FileHeader.TimeDateStamp;
			(void)add_range(state, base, (std::min)(result.size, std::uint64_t{0x1000}), range_kind::headers);
			if (headers.OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXCEPTION)
			{
				const auto& table = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
				if (table.VirtualAddress <= result.size && table.Size <= result.size - table.VirtualAddress &&
					table.Size && table.Size % sizeof(RUNTIME_FUNCTION) == 0)
				{
					result.exception_rva = table.VirtualAddress;
					result.exception_size = table.Size;
					if (table.Size <= max_table_bytes)
						result.table_scheduled = add_range(state, base + table.VirtualAddress, table.Size, range_kind::function_table);
					else { ++report.skipped; report.limits |= 128; }
				}
			}
			return &result;
		}

		bool function_at(const module& image, const std::uint64_t pc, RUNTIME_FUNCTION& result) noexcept
		{
			if (pc < image.base || pc - image.base >= image.size) return false;
			auto low = std::uint64_t{};
			auto high = image.exception_size / sizeof(RUNTIME_FUNCTION);
			while (low < high)
			{
				const auto middle = low + (high - low) / 2;
				RUNTIME_FUNCTION entry{};
				if (!read(image.base + image.exception_rva + middle * sizeof(entry), &entry, sizeof(entry))) return false;
				if (pc - image.base < entry.BeginAddress) high = middle;
				else if (pc - image.base >= entry.EndAddress) low = middle + 1;
				else { result = entry; return true; }
			}
			return false;
		}

		void add_unwind(plan& state, const module& image, DWORD rva) noexcept
		{
			for (unsigned depth{}; depth < 8; ++depth)
			{
				std::array<std::uint8_t, 4> header{};
				if (rva > image.size || sizeof(header) > image.size - rva ||
					!read(image.base + rva, header.data(), header.size())) { ++state.report.skipped; return; }
				const auto flags = header[0] >> 3;
				const auto tail = 4u + ((static_cast<unsigned>(header[2]) * 2 + 3) & ~3u);
				const auto size = tail + ((flags & 4) ? sizeof(RUNTIME_FUNCTION) : ((flags & 3) ? sizeof(DWORD) : 0));
				if (size > image.size - rva || !add_range(state, image.base + rva, size, range_kind::unwind_info)) return;
				if (!(flags & 4)) return;
				RUNTIME_FUNCTION chained{};
				if (!read(image.base + rva + tail, &chained, sizeof(chained))) return;
				rva = chained.UnwindData;
			}
			++state.report.skipped;
			state.report.limits |= 16;
		}

		void add_pc(plan& state, const std::uint64_t pc) noexcept
		{
			auto& report = state.report;
			if (!pc) return;
			for (std::size_t i{}; i < report.candidate_count; ++i) if (state.candidates[i] == pc) return;
			MEMORY_BASIC_INFORMATION region{};
			if (!VirtualQuery(reinterpret_cast<const void*>(pc), &region, sizeof(region)) || region.State != MEM_COMMIT ||
				!readable(region.Protect) || !(region.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return;
			if (report.candidate_count == state.candidates.size()) { ++report.skipped; report.limits |= 8; return; }
			state.candidates[report.candidate_count++] = pc;
			(void)add_range(state, pc >= 64 ? pc - 64 : pc, 128, range_kind::code);
			if (region.Type != MEM_IMAGE) return;
			const auto* image = find_module(state, reinterpret_cast<std::uint64_t>(region.AllocationBase));
			if (!image) return;
			RUNTIME_FUNCTION entry{};
			if (!function_at(*image, pc, entry)) return;
			add_unwind(state, *image, entry.UnwindData);
			if (entry.BeginAddress < image->size)
				(void)add_range(state, image->base + entry.BeginAddress,
					(std::min)(std::uint64_t{64}, image->size - entry.BeginAddress), range_kind::code);
		}

		bool capture_context(CONTEXT& context, const DWORD thread) noexcept
		{
			if (thread == GetCurrentThreadId()) { RtlCaptureContext(&context); return true; }
			const auto handle = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, thread);
			if (!handle) return false;
			bool result{};
			if (SuspendThread(handle) != static_cast<DWORD>(-1))
			{
				context.ContextFlags = CONTEXT_FULL;
				result = GetThreadContext(handle, &context) != FALSE;
				const auto resumed = ResumeThread(handle);
				if (resumed == static_cast<DWORD>(-1)) result = false;
			}
			CloseHandle(handle);
			return result;
		}
	}

	void prepare(plan& output, const EXCEPTION_POINTERS* exception, DWORD focus_thread) noexcept
	{
		std::memset(&output, 0, sizeof(output));
		auto& report = output.report;
		report.version = 1;
		report.budget = byte_budget;
		report.stack_candidates_only = 1;
		report.sampled_tick = GetTickCount64();
		if (!focus_thread) focus_thread = GetCurrentThreadId();
		report.focus_thread = focus_thread;
		CONTEXT context{};
		if (exception)
		{
			EXCEPTION_POINTERS pointers{};
			EXCEPTION_RECORD record{};
			if (!read(reinterpret_cast<std::uint64_t>(exception), &pointers, sizeof(pointers)) ||
				!read(reinterpret_cast<std::uint64_t>(pointers.ContextRecord), &context, sizeof(context)))
			{ ++report.skipped; report.limits |= 32; return; }
			if (read(reinterpret_cast<std::uint64_t>(pointers.ExceptionRecord), &record, sizeof(record)))
				report.exception_code = record.ExceptionCode;
		}
		else if (!capture_context(context, focus_thread)) { ++report.skipped; report.limits |= 32; return; }
		report.context_available = 1;
		report.pc = context.Rip;
		report.stack_pointer = context.Rsp;
		add_pc(output, context.Rip);
		const std::array<std::uint64_t, 14> registers{context.Rax, context.Rbx, context.Rcx, context.Rdx,
			context.Rsi, context.Rdi, context.R8, context.R9, context.R10, context.R11, context.R12, context.R13, context.R14, context.R15};
		for (const auto value : registers)
		{
			if (value) (void)add_range(output, value, 64, range_kind::registers);
		}
		// Stack contents only nominate possible PCs; they are never reported as
		// a proven call chain. The captured .pdata/.xdata enables offline unwind.
		std::array<std::uint64_t, 64> words{};
		for (std::size_t offset{}; offset < 16 * 1024; offset += sizeof(words))
		{
			if (context.Rsp > (std::numeric_limits<std::uint64_t>::max)() - offset ||
				!read(context.Rsp + offset, words.data(), sizeof(words))) { ++report.skipped; report.limits |= 64; break; }
			report.scanned_stack_bytes += sizeof(words);
			for (const auto word : words) add_pc(output, word);
		}
	}

	BOOL CALLBACK callback(void* context, const PMINIDUMP_CALLBACK_INPUT input, MINIDUMP_CALLBACK_OUTPUT* output) noexcept
	{
		if (!context || !input || !output) return FALSE;
		auto& state = *static_cast<plan*>(context);
		switch (input->CallbackType)
		{
		case MemoryCallback:
			if (state.next_range == state.report.range_count) return FALSE;
			output->MemoryBase = state.report.ranges[state.next_range].address;
			output->MemorySize = static_cast<ULONG>(state.report.ranges[state.next_range++].size);
			return TRUE;
		case RemoveMemoryCallback: return FALSE;
		case CancelCallback: output->CheckCancel = FALSE; output->Cancel = FALSE; return TRUE;
		case IoStartCallback: output->Status = S_OK; return TRUE;
		case VmStartCallback: output->Status = S_OK; return TRUE;
		// S_FALSE identifies a PSS snapshot handle. These writers pass a real
		// process handle; using S_FALSE makes DbgCore treat -1 as a snapshot.
		case IsProcessSnapshotCallback: output->Status = S_OK; return TRUE;
		case WriteKernelMinidumpCallback: return FALSE;
		case SecondaryFlagsCallback: output->SecondaryFlags = 0; return TRUE;
		case ReadMemoryFailureCallback: output->Status = S_OK; return TRUE;
		default: return TRUE;
		}
	}
}
