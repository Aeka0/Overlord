#include <std_include.hpp>
#include "launch_process.hpp"
#include "bridge_protocol.hpp"

namespace launcher_process
{
	namespace
	{
		std::filesystem::path executable_path()
		{
			std::wstring path(32768, L'\0');
			const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
			if (!size || size >= path.size()) throw std::runtime_error("Could not locate the launcher executable.");
			path.resize(size);
			return path;
		}
	}

	void use_executable_directory()
	{
		std::filesystem::current_path(executable_path().parent_path());
	}

	void start_game(bool unlock_campaign)
	{
		const auto executable = executable_path();
		auto command = launcher_bridge::quote_argument(executable.wstring());
		int count{};
		auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!arguments) throw std::runtime_error("Could not read startup arguments.");
		const auto release = gsl::finally([&] { LocalFree(arguments); });
		for (int i = 1; i < count; ++i)
		{
			// This window owns the request; do not inherit a stale command-line value.
			if (_wcsicmp(arguments[i], L"-campaign-unlock") == 0)
			{
				if (i + 1 < count && arguments[i + 1][0] != L'-') ++i;
				continue;
			}
			command += L" " + launcher_bridge::quote_argument(arguments[i]);
		}
		command += L" -singleplayer -launcher-start";
		command += unlock_campaign ? L" -campaign-unlock 1" : L" -campaign-unlock 0";
		if (command.size() >= 32767) throw std::runtime_error("The startup command is too long.");
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const auto directory = std::filesystem::current_path().wstring();
		if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
			nullptr, directory.c_str(), &startup, &process)) throw std::runtime_error("Could not start the game process.");
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
	}
}
