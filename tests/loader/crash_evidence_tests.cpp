#include <exception/dump_evidence.hpp>
#include <exception/minidump.hpp>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
	unsigned checks{};
	std::vector<char> dump;
	const MINIDUMP_MEMORY_LIST* memory_list{};
	const MINIDUMP_MODULE_LIST* module_list{};
	RUNTIME_FUNCTION current_function{};

	void require(const bool value, const char* message)
	{
		++checks;
		if (!value) throw std::runtime_error(message);
	}

	BOOL CALLBACK memory(HANDLE, DWORD64 address, void* output, DWORD count, DWORD* received)
	{
		*received = 0;
		for (ULONG index{}; index < memory_list->NumberOfMemoryRanges; ++index)
		{
			const auto& range = memory_list->MemoryRanges[index];
			if (address < range.StartOfMemoryRange || address - range.StartOfMemoryRange >= range.Memory.DataSize) continue;
			const auto size = (std::min)(static_cast<ULONG64>(count), range.Memory.DataSize - (address - range.StartOfMemoryRange));
			const auto offset = range.Memory.Rva + address - range.StartOfMemoryRange;
			if (offset > dump.size() || size > dump.size() - offset) return FALSE;
			std::memcpy(output, dump.data() + offset, static_cast<std::size_t>(size));
			*received = static_cast<DWORD>(size);
			return TRUE;
		}
		return FALSE;
	}

	template <typename T> bool read(const DWORD64 address, T& value)
	{
		DWORD received{};
		return memory(nullptr, address, &value, sizeof(value), &received) && received == sizeof(value);
	}

	DWORD64 CALLBACK module_base(HANDLE, const DWORD64 address)
	{
		for (ULONG index{}; index < module_list->NumberOfModules; ++index)
		{
			const auto& module = module_list->Modules[index];
			if (address >= module.BaseOfImage && address - module.BaseOfImage < module.SizeOfImage) return module.BaseOfImage;
		}
		return 0;
	}

	void* CALLBACK function_table(HANDLE process, const DWORD64 address)
	{
		const auto base = module_base(process, address);
		IMAGE_DOS_HEADER dos{};
		IMAGE_NT_HEADERS64 headers{};
		if (!base || !read(base, dos) || dos.e_lfanew < 0 || !read(base + dos.e_lfanew, headers)) return nullptr;
		const auto& table = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
		ULONG low{}, high = table.Size / sizeof(RUNTIME_FUNCTION);
		while (low < high)
		{
			const auto middle = low + (high - low) / 2;
			RUNTIME_FUNCTION entry{};
			if (!read(base + table.VirtualAddress + middle * sizeof(entry), entry)) return nullptr;
			if (address - base < entry.BeginAddress) high = middle;
			else if (address - base >= entry.EndAddress) low = middle + 1;
			else { current_function = entry; return &current_function; }
		}
		return nullptr;
	}

	__declspec(noinline) bool write_leaf(const std::string& path)
	{
		CONTEXT context{};
		RtlCaptureContext(&context);
		EXCEPTION_RECORD record{};
		record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
		record.ExceptionAddress = reinterpret_cast<void*>(context.Rip);
		record.NumberParameters = 2;
		record.ExceptionInformation[1] = 0x250;
		EXCEPTION_POINTERS pointers{&record, &context};
		return exception::write_exception_minidump(path.c_str(), &pointers, GetCurrentThreadId());
	}

	volatile unsigned completed{};
	__declspec(noinline) bool write_middle(const std::string& path)
	{
		const auto written = write_leaf(path);
		completed = completed + 1;
		return written;
	}
	__declspec(noinline) bool write_outer(const std::string& path)
	{
		const auto written = write_middle(path);
		completed = completed + 1;
		return written;
	}

	template <typename T> const T* stream(const ULONG type)
	{
		MINIDUMP_DIRECTORY* directory{};
		void* data{};
		ULONG size{};
		require(MiniDumpReadDumpStream(dump.data(), type, &directory, &data, &size) && size >= sizeof(T), "required dump stream missing");
		return static_cast<const T*>(data);
	}

	void load_dump(const std::filesystem::path& path)
	{
		std::ifstream source(path, std::ios::binary);
		dump.assign(std::istreambuf_iterator<char>(source), {});
		require(!dump.empty(), "dump file is empty");
		memory_list = stream<MINIDUMP_MEMORY_LIST>(MemoryListStream);
		module_list = stream<MINIDUMP_MODULE_LIST>(ModuleListStream);
	}

	void exception_dump(const std::filesystem::path& path)
	{
		require(write_outer(path.string()), "exception dump write failed");
		load_dump(path);
		const auto* evidence = stream<exception::evidence::stream>(exception::evidence::stream_type);
		require(evidence->version == 1 && evidence->focus_thread == GetCurrentThreadId() && evidence->context_available,
			"exception evidence has wrong focus identity");
		require(evidence->requested_bytes <= exception::evidence::byte_budget &&
			evidence->range_count <= exception::evidence::max_ranges && evidence->module_count <= exception::evidence::max_modules,
			"extra evidence exceeded its bounds");
		const auto base = module_base(nullptr, evidence->pc);
		require(base != 0 && function_table(nullptr, evidence->pc), "captured headers/function table cannot locate fault function");
		DWORD unwind_header{};
		require(read(base + current_function.UnwindData, unwind_header), "fault unwind data not captured");

		const auto* exception_stream = stream<MINIDUMP_EXCEPTION_STREAM>(ExceptionStream);
		CONTEXT context{};
		std::memcpy(&context, dump.data() + exception_stream->ThreadContext.Rva, sizeof(context));
		STACKFRAME64 frame{};
		frame.AddrPC = {context.Rip, 0, AddrModeFlat};
		frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
		frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
		unsigned frames{};
		for (unsigned index{}; index < 16; ++index)
		{
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), nullptr, &frame, &context,
				memory, function_table, module_base, nullptr) || !frame.AddrPC.Offset) break;
			++frames;
		}
		require(frames >= 4, "offline unwind failed without module files");
		std::printf("offline captured-memory-only frames=%u\n", frames);
	}

	struct worker_state { HANDLE ready{}, finish{}; };
	DWORD WINAPI worker(void* argument)
	{
		const auto& state = *static_cast<worker_state*>(argument);
		SetEvent(state.ready);
		return WaitForSingleObject(state.finish, 5000) == WAIT_OBJECT_0 ? 0 : 1;
	}

	void watchdog_dump(const std::filesystem::path& path)
	{
		worker_state state{CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
		require(state.ready && state.finish, "worker events unavailable");
		DWORD thread_id{};
		const auto thread = CreateThread(nullptr, 0, worker, &state, 0, &thread_id);
		require(thread && WaitForSingleObject(state.ready, 5000) == WAIT_OBJECT_0, "worker did not start");
		const bool written = exception::write_process_minidump(path.string(), thread_id);
		SetEvent(state.finish);
		require(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "focused thread left suspended");
		CloseHandle(thread);
		CloseHandle(state.ready);
		CloseHandle(state.finish);
		require(written, "watchdog dump write failed");
		load_dump(path);
		const auto* evidence = stream<exception::evidence::stream>(exception::evidence::stream_type);
		require(evidence->focus_thread == thread_id && evidence->context_available && evidence->range_count,
			"watchdog evidence sampled the writer instead of the focused thread");
	}

	void invalid_context()
	{
		exception::evidence::plan plan;
		MINIDUMP_CALLBACK_INPUT input{};
		MINIDUMP_CALLBACK_OUTPUT callback_output{};
		input.CallbackType = IsProcessSnapshotCallback;
		callback_output.Status = S_FALSE;
		require(exception::evidence::callback(&plan, &input, &callback_output) && callback_output.Status == S_OK,
			"live process misidentified as a PSS snapshot");
		exception::evidence::prepare(plan, reinterpret_cast<EXCEPTION_POINTERS*>(1), GetCurrentThreadId());
		require(!plan.report.context_available && !plan.report.range_count, "invalid exception pointer accepted");
		CONTEXT context{};
		context.Rip = 1;
		context.Rsp = (std::numeric_limits<std::uint64_t>::max)() - 8;
		context.Rcx = (std::numeric_limits<std::uint64_t>::max)();
		EXCEPTION_RECORD record{};
		EXCEPTION_POINTERS pointers{&record, &context};
		exception::evidence::prepare(plan, &pointers, GetCurrentThreadId());
		require(!plan.report.range_count && plan.report.skipped, "inaccessible context ranges were scheduled");
	}
}

int wmain(int argc, wchar_t** argv)
{
	wchar_t temporary[MAX_PATH]{};
	if (!GetTempPathW(MAX_PATH, temporary)) return 2;
	const auto output = argc > 1 ? std::filesystem::path(argv[1]) :
		std::filesystem::path(temporary) / (L"overlord-crash-evidence-" + std::to_wstring(GetCurrentProcessId()));
	try
	{
		std::filesystem::create_directories(output);
		invalid_context();
		exception_dump(output / L"exception.dmp");
		watchdog_dump(output / L"watchdog.dmp");
		std::printf("PASS: bounded crash evidence checks=%u\n", checks);
		if (argc == 1) std::filesystem::remove_all(output);
		return 0;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		return 1;
	}
}
