#include "minidump.hpp"
#include "dump_evidence.hpp"

#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <atomic>
#include <gsl/gsl>

namespace exception
{
	namespace
	{
		// DbgHelp is process-global and single-threaded.  A crash path must never
		// wait for a diagnostic dump already in progress, so all public writers
		// share this try-only gate.
		std::atomic_flag minidump_write_in_progress = ATOMIC_FLAG_INIT;
		// Serialized by the existing try-only gate, including the emergency
		// writer. Keep the bounded plan off a potentially exhausted crash stack.
		evidence::plan dump_plan;

		BOOL write_evidence_dump(HANDLE file, MINIDUMP_EXCEPTION_INFORMATION* exception,
			const EXCEPTION_POINTERS* pointers, DWORD focus_thread, MINIDUMP_TYPE type) noexcept
		{
			evidence::prepare(dump_plan, pointers, focus_thread);
			MINIDUMP_USER_STREAM stream{evidence::stream_type, sizeof(dump_plan.report), &dump_plan.report};
			MINIDUMP_USER_STREAM_INFORMATION streams{1, &stream};
			MINIDUMP_CALLBACK_INFORMATION callback{evidence::callback, &dump_plan};
			return MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
				exception, &streams, &callback);
		}

		class minidump_write_gate final
		{
		public:
			minidump_write_gate() noexcept
				: acquired_(!minidump_write_in_progress.test_and_set(std::memory_order_acquire))
			{
			}

			~minidump_write_gate() noexcept
			{
				if (acquired_)
				{
					minidump_write_in_progress.clear(std::memory_order_release);
				}
			}

			minidump_write_gate(const minidump_write_gate&) = delete;
			minidump_write_gate& operator=(const minidump_write_gate&) = delete;

			explicit operator bool() const noexcept
			{
				return acquired_;
			}

		private:
			bool acquired_{};
		};

		constexpr MINIDUMP_TYPE get_minidump_type()
		{
			const auto type = MiniDumpIgnoreInaccessibleMemory //
				| MiniDumpWithHandleData //
				| MiniDumpScanMemory //
				| MiniDumpWithProcessThreadData //
				| MiniDumpWithFullMemoryInfo //
				| MiniDumpWithThreadInfo //
				| MiniDumpWithUnloadedModules;

			return static_cast<MINIDUMP_TYPE>(type);
		}

		std::string get_temp_filename()
		{
			char filename[MAX_PATH] = {0};
			char pathname[MAX_PATH] = {0};

			GetTempPathA(sizeof(pathname), pathname);
			GetTempFileNameA(pathname, "H2-Mod-", 0, filename);
			return filename;
		}

		HANDLE write_dump_to_temp_file(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			MINIDUMP_EXCEPTION_INFORMATION minidump_exception_info = {GetCurrentThreadId(), exceptioninfo, FALSE};

			auto* const file_handle = CreateFileA(get_temp_filename().data(), GENERIC_WRITE | GENERIC_READ,
			                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
			                                      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
			                                      nullptr);

			if (!write_evidence_dump(file_handle, &minidump_exception_info, exceptioninfo,
				GetCurrentThreadId(), get_minidump_type()))
			{
				MessageBoxA(nullptr, "There was an error creating the minidump! Hit OK to close the program.",
				            "Minidump Error", MB_OK | MB_ICONERROR);
				TerminateProcess(GetCurrentProcess(), 123);
			}

			return file_handle;
		}

		std::string read_file(HANDLE file_handle)
		{
			FlushFileBuffers(file_handle);
			SetFilePointer(file_handle, 0, nullptr, FILE_BEGIN);

			std::string buffer{};

			DWORD bytes_read = 0;
			char temp_bytes[0x2000];

			do
			{
				if (!ReadFile(file_handle, temp_bytes, sizeof(temp_bytes), &bytes_read, nullptr))
				{
					return {};
				}

				buffer.append(temp_bytes, bytes_read);
			}
			while (bytes_read == sizeof(temp_bytes));

			return buffer;
		}
	}

	std::string create_minidump(const LPEXCEPTION_POINTERS exceptioninfo)
	{
		const minidump_write_gate gate;
		if (!gate)
		{
			return {};
		}

		auto* const file_handle = write_dump_to_temp_file(exceptioninfo);

		const auto _ = gsl::finally([file_handle]()
		{
			CloseHandle(file_handle);
		});

		return read_file(file_handle);
	}

	bool write_process_minidump(const std::string& path, const DWORD focus_thread_id)
	{
		const minidump_write_gate gate;
		if (!gate)
		{
			return false;
		}

		auto* const file_handle = CreateFileA(path.c_str(), GENERIC_WRITE | GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file_handle == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		const auto close = gsl::finally([file_handle]() { CloseHandle(file_handle); });
		return write_evidence_dump(file_handle, nullptr, nullptr, focus_thread_id, get_minidump_type()) != FALSE;
	}

	bool write_exception_minidump(const char* const path,
		const LPEXCEPTION_POINTERS exceptioninfo, const DWORD exception_thread_id) noexcept
	{
		if (path == nullptr || path[0] == '\0' || exceptioninfo == nullptr)
		{
			return false;
		}

		const minidump_write_gate gate;
		if (!gate)
		{
			return false;
		}

		const auto file_handle = CreateFileA(path, GENERIC_WRITE | GENERIC_READ,
			FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file_handle == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		MINIDUMP_EXCEPTION_INFORMATION exception_information{
			exception_thread_id, exceptioninfo, FALSE,
		};
		const auto written = write_evidence_dump(file_handle, &exception_information, exceptioninfo,
			exception_thread_id, get_minidump_type()) != FALSE;
		FlushFileBuffers(file_handle);
		CloseHandle(file_handle);
		return written;
	}
}
