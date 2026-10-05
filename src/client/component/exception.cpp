#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "scheduler.hpp"

#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>
#include <utils/thread.hpp>
#include <utils/compression.hpp>

#include <exception/minidump.hpp>

#include <version.hpp>

#include "game/dvars.hpp"
#include "vr/diagnostics.hpp"

namespace exception
{
	namespace
	{
		thread_local struct
		{
			DWORD code = 0;
			PVOID address = nullptr;
		} exception_data;

		struct
		{
			std::chrono::time_point<std::chrono::high_resolution_clock> last_recovery{};
			std::atomic<int> recovery_counts = {0};
		} recovery_data;

		struct emergency_dump_state
		{
			HANDLE request_event{};
			HANDLE completed_event{};
			HANDLE writer_thread{};
			volatile LONG capture_started{};
			volatile LONG dump_written{};
			DWORD exception_thread_id{};
			EXCEPTION_RECORD exception_record{};
			CONTEXT context_record{};
			EXCEPTION_POINTERS exception_pointers{};
			char dump_path[MAX_PATH]{};
			char report_path[MAX_PATH]{};
			char report[1024]{};
			DWORD report_size{};
		};

		emergency_dump_state emergency_dump;

		DWORD WINAPI emergency_dump_writer(void*) noexcept
		{
			for (;;)
			{
				if (WaitForSingleObject(emergency_dump.request_event, INFINITE) != WAIT_OBJECT_0)
				{
					return 1;
				}

				const auto dump_written = exception::write_exception_minidump(
					emergency_dump.dump_path, &emergency_dump.exception_pointers,
					emergency_dump.exception_thread_id);
				emergency_dump.dump_written = dump_written ? 1 : 0;

				const auto report_file = CreateFileA(emergency_dump.report_path, GENERIC_WRITE,
					FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (report_file != INVALID_HANDLE_VALUE)
				{
					DWORD bytes_written{};
					WriteFile(report_file, emergency_dump.report, emergency_dump.report_size,
						&bytes_written, nullptr);
					FlushFileBuffers(report_file);
					CloseHandle(report_file);
				}
				SetEvent(emergency_dump.completed_event);
			}
		}

		void initialize_emergency_dump_writer() noexcept
		{
			emergency_dump.request_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
			emergency_dump.completed_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
			if (emergency_dump.request_event == nullptr || emergency_dump.completed_event == nullptr)
			{
				return;
			}
			emergency_dump.writer_thread = CreateThread(nullptr, 0, emergency_dump_writer,
				nullptr, 0, nullptr);
		}

		bool write_emergency_dump(const LPEXCEPTION_POINTERS exceptioninfo) noexcept
		{
			if (exceptioninfo == nullptr || exceptioninfo->ExceptionRecord == nullptr ||
				exceptioninfo->ContextRecord == nullptr || emergency_dump.writer_thread == nullptr ||
				InterlockedCompareExchange(&emergency_dump.capture_started, 1, 0) != 0)
			{
				return false;
			}

			CreateDirectoryA("minidumps", nullptr);
			SYSTEMTIME timestamp{};
			GetLocalTime(&timestamp);
			emergency_dump.exception_thread_id = GetCurrentThreadId();
			emergency_dump.exception_record = *exceptioninfo->ExceptionRecord;
			emergency_dump.context_record = *exceptioninfo->ContextRecord;
			emergency_dump.exception_pointers = {
				&emergency_dump.exception_record, &emergency_dump.context_record,
			};

			sprintf_s(emergency_dump.dump_path,
				"minidumps/h2-mod-emergency-%04u-%02u-%02u-%02u-%02u-%02u-p%lu-t%lu.dmp",
				timestamp.wYear, timestamp.wMonth, timestamp.wDay, timestamp.wHour,
				timestamp.wMinute, timestamp.wSecond, GetCurrentProcessId(),
				emergency_dump.exception_thread_id);
			sprintf_s(emergency_dump.report_path,
				"minidumps/h2-mod-emergency-%04u-%02u-%02u-%02u-%02u-%02u-p%lu-t%lu.txt",
				timestamp.wYear, timestamp.wMonth, timestamp.wDay, timestamp.wHour,
				timestamp.wMinute, timestamp.wSecond, GetCurrentProcessId(),
				emergency_dump.exception_thread_id);
			const auto report_size = sprintf_s(emergency_dump.report,
				"H2-MOD VR emergency crash evidence\r\n"
				"Exception: 0x%08lX\r\nAddress: 0x%p\r\nProcess/thread: %lu/%lu\r\n"
				"RIP/RSP/RBP: 0x%llX/0x%llX/0x%llX\r\n"
				"Parameter count: %lu\r\nParameter 0/1/2: 0x%llX/0x%llX/0x%llX\r\n",
				emergency_dump.exception_record.ExceptionCode,
				emergency_dump.exception_record.ExceptionAddress, GetCurrentProcessId(),
				emergency_dump.exception_thread_id, emergency_dump.context_record.Rip,
				emergency_dump.context_record.Rsp, emergency_dump.context_record.Rbp,
				emergency_dump.exception_record.NumberParameters,
				emergency_dump.exception_record.ExceptionInformation[0],
				emergency_dump.exception_record.ExceptionInformation[1],
				emergency_dump.exception_record.ExceptionInformation[2]);
			emergency_dump.report_size = report_size > 0
				? static_cast<DWORD>(report_size) : 0;

			emergency_dump.dump_written = 0;
			ResetEvent(emergency_dump.completed_event);
			if (!SetEvent(emergency_dump.request_event))
			{
				return false;
			}
			if (WaitForSingleObject(emergency_dump.completed_event, 15000) != WAIT_OBJECT_0)
			{
				return false;
			}
			return emergency_dump.dump_written != 0;
		}

		bool is_game_thread()
		{
			static std::vector<int> allowed_threads =
			{
				game::THREAD_CONTEXT_MAIN,
			};

			const auto self_id = GetCurrentThreadId();
			for (const auto& index : allowed_threads)
			{
				if (game::threadIds[index] == self_id)
				{
					return true;
				}
			}

			return false;
		}

		bool is_exception_interval_too_short()
		{
			const auto delta = std::chrono::high_resolution_clock::now() - recovery_data.last_recovery;
			return delta < 1min;
		}

		bool too_many_exceptions_occured()
		{
			return recovery_data.recovery_counts >= 3;
		}

		volatile bool& is_initialized()
		{
			static volatile bool initialized = false;
			return initialized;
		}

		bool is_recoverable()
		{
			return is_initialized()
				&& is_game_thread()
				&& !is_exception_interval_too_short()
				&& !too_many_exceptions_occured();
		}

		void show_mouse_cursor()
		{
			while (ShowCursor(TRUE) < 0);
		}

		void display_error_dialog()
		{
			std::string error_str = utils::string::va("Fatal error (0x%08X) at 0x%p.\n"
			                                          "A minidump has been written.\n\n",
			                                          exception_data.code, exception_data.address);

			error_str += "Make sure to update your graphics card drivers and install operating system updates!";

			utils::thread::suspend_other_threads();
			show_mouse_cursor();

			MessageBoxA(nullptr, error_str.data(), "h2-mod-vr ERROR", MB_ICONERROR);
			TerminateProcess(GetCurrentProcess(), exception_data.code);
		}

		void reset_state()
		{
			if (dvars::cg_legacyCrashHandling && dvars::cg_legacyCrashHandling->current.enabled)
			{
				display_error_dialog();
			}

			if (is_recoverable())
			{
				recovery_data.last_recovery = std::chrono::high_resolution_clock::now();
				++recovery_data.recovery_counts;
				game::Com_Error(game::ERR_DROP, "Fatal error (0x%08X) at 0x%p.\nA minidump has been written.\n\n"
				                "h2-mod-vr has tried to recover your game, but it might not run stable anymore.\n\n"
				                "Make sure to update your graphics card drivers and install operating system updates!\n",
				                exception_data.code, exception_data.address);
			}
			else
			{
				display_error_dialog();
			}
		}

		size_t get_reset_state_stub()
		{
			static auto* stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.sub(rsp, 0x10);
				a.or_(rsp, 0x8);
				a.jmp(reset_state);
			});

			return reinterpret_cast<size_t>(stub);
		}

		std::string get_timestamp()
		{
			tm ltime{};
			char timestamp[MAX_PATH] = {0};
			const auto time = _time64(nullptr);

			_localtime64_s(&ltime, &time);
			strftime(timestamp, sizeof(timestamp) - 1, "%Y-%m-%d-%H-%M-%S", &ltime);

			return timestamp;
		}

		std::string generate_crash_info(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			std::string info{};
			const auto line = [&info](const std::string& text)
			{
				info.append(text);
				info.append("\r\n");
			};

			line("H2-MOD VR Crash Dump");
			line("");
			line("Version: "s + VERSION);
			line("Environment: "s + game::environment::get_string());
			line("Timestamp: "s + get_timestamp());
			line(utils::string::va("Exception: 0x%08X", exceptioninfo->ExceptionRecord->ExceptionCode));
			line(utils::string::va("Address: 0x%llX", exceptioninfo->ExceptionRecord->ExceptionAddress));
			info.append(vr::diagnostics::crash_trace());

#pragma warning(push)
#pragma warning(disable: 4996)
			OSVERSIONINFOEXA version_info;
			ZeroMemory(&version_info, sizeof(version_info));
			version_info.dwOSVersionInfoSize = sizeof(version_info);
			GetVersionExA(reinterpret_cast<LPOSVERSIONINFOA>(&version_info));
#pragma warning(pop)

			line(utils::string::va("OS Version: %u.%u", version_info.dwMajorVersion, version_info.dwMinorVersion));

			return info;
		}

		void write_minidump(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			const std::string crash_stem = utils::string::va("minidumps/h2-mod-crash-%d-%s",
			                                                 game::environment::get_real_mode(),
			                                                 get_timestamp().data());
			const auto dump = create_minidump(exceptioninfo);
			const auto info = generate_crash_info(exceptioninfo);

			// Keep raw evidence before invoking the ZIP writer.  The exception path
			// itself can fault inside third-party compression/locale code; sidecars
			// must remain usable even when the historical archive cannot be closed.
			utils::io::write_file(crash_stem + ".dmp", dump, false);
			utils::io::write_file(crash_stem + ".txt", info, false);

			utils::compression::zip::archive zip_file{};
			zip_file.add("crash.dmp", dump);
			zip_file.add("info.txt", info);
			zip_file.write(crash_stem + ".zip", "h2-mod-vr Crash Dump");
		}

		bool is_harmless_error(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			const auto code = exceptioninfo->ExceptionRecord->ExceptionCode;
			return code == STATUS_INTEGER_OVERFLOW || code == STATUS_FLOAT_OVERFLOW || code == STATUS_SINGLE_STEP;
		}

		LONG WINAPI exception_filter(const LPEXCEPTION_POINTERS exceptioninfo)
		{
			if (is_harmless_error(exceptioninfo))
			{
				return EXCEPTION_CONTINUE_EXECUTION;
			}

			// Capture the exception from a pre-created dedicated thread before the
			// historical report/ZIP path allocates memory. Heap or stack corruption can
			// fault again inside std::string/compression and otherwise leave only WER's
			// protected archive.
			(void)write_emergency_dump(exceptioninfo);
			write_minidump(exceptioninfo);

			exception_data.code = exceptioninfo->ExceptionRecord->ExceptionCode;
			exception_data.address = exceptioninfo->ExceptionRecord->ExceptionAddress;
			exceptioninfo->ContextRecord->Rip = get_reset_state_stub();

			return EXCEPTION_CONTINUE_EXECUTION;
		}

		LPTOP_LEVEL_EXCEPTION_FILTER WINAPI set_unhandled_exception_filter_stub(LPTOP_LEVEL_EXCEPTION_FILTER)
		{
			// Don't register anything here...
			return &exception_filter;
		}
	}

	class component final : public component_interface
	{
	public:
		component()
		{
			initialize_emergency_dump_writer();
			SetUnhandledExceptionFilter(exception_filter);
		}

		void post_load() override
		{
			SetUnhandledExceptionFilter(exception_filter);
			utils::hook::jump(SetUnhandledExceptionFilter, set_unhandled_exception_filter_stub, true);

			scheduler::on_game_initialized([]()
			{
				is_initialized() = true;
			});
		}

		void post_unpack() override
		{
			dvars::cg_legacyCrashHandling = dvars::register_bool("cg_legacyCrashHandling", false, 
				game::DVAR_FLAG_SAVED, "Disable new crash handling");
		}
	};
}

REGISTER_COMPONENT(exception::component)
