#pragma once

#include "../utils/nt.hpp"

namespace exception
{
	// Dump writers use one process-wide try-only gate. Concurrent calls return
	// an empty string/false immediately instead of waiting in a crash path.
	std::string create_minidump(LPEXCEPTION_POINTERS exceptioninfo);
	bool write_process_minidump(const std::string& path);
	// Crash-path primitive: writes directly to the final file without first
	// materializing the dump in a std::string. exception_thread_id identifies
	// the thread whose exception pointers are supplied when a dedicated writer
	// thread performs MiniDumpWriteDump.
	bool write_exception_minidump(const char* path, LPEXCEPTION_POINTERS exceptioninfo,
		DWORD exception_thread_id) noexcept;
}
