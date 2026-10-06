#include <std_include.hpp>

#include "steamvr_runtime.hpp"

#include <openvr.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <optional>

#include <sddl.h>
#include <TlHelp32.h>

#include <json.hpp>

namespace vr::steamvr
{
	namespace
	{
		constexpr wchar_t active_runtime_key[] = L"SOFTWARE\\Khronos\\OpenXR\\1";

		std::optional<std::wstring> read_environment(const wchar_t* const name)
		{
			const auto required = GetEnvironmentVariableW(name, nullptr, 0);
			if (required == 0) return std::nullopt;
			std::wstring value(required, L'\0');
			const auto written = GetEnvironmentVariableW(name, value.data(), required);
			if (written == 0 || written >= required) return std::nullopt;
			value.resize(written);
			return value;
		}

		std::optional<std::filesystem::path> read_active_manifest()
		{
			if (const auto override_path = read_environment(L"XR_RUNTIME_JSON"); override_path.has_value())
			{
				return std::filesystem::path(*override_path);
			}
			// Match the bundled OpenXR loader's Windows ActiveRuntime lookup.
			for (const auto root : {HKEY_LOCAL_MACHINE})
			{
				DWORD type{};
				DWORD bytes{};
				const auto flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_SUBKEY_WOW6464KEY;
				if (RegGetValueW(root, active_runtime_key, L"ActiveRuntime", flags,
					&type, nullptr, &bytes) != ERROR_SUCCESS || bytes <= sizeof(wchar_t))
				{
					continue;
				}
				std::wstring value(bytes / sizeof(wchar_t), L'\0');
				if (RegGetValueW(root, active_runtime_key, L"ActiveRuntime", flags,
					&type, value.data(), &bytes) == ERROR_SUCCESS)
				{
					value.resize(std::wcslen(value.c_str()));
					return std::filesystem::path(value);
				}
			}
			return std::nullopt;
		}

		std::optional<std::wstring> widen_utf8(const std::string_view value)
		{
			if (value.empty()) return std::wstring{};
			const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), nullptr, 0);
			if (required <= 0) return std::nullopt;
			std::wstring result(static_cast<std::size_t>(required), L'\0');
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), result.data(), required) != required)
			{
				return std::nullopt;
			}
			return result;
		}

		std::string utf8_path(const std::filesystem::path& path)
		{
			const auto& value = path.native();
			const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
			if (required <= 0) return "<unprintable>";
			std::string result(static_cast<std::size_t>(required), '\0');
			(void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
			return result;
		}

		bool is_steamvr_manifest(const std::filesystem::path& path)
		{
			auto name = path.filename().wstring();
			std::ranges::transform(name, name.begin(), [](const wchar_t value)
			{
				return static_cast<wchar_t>(std::towlower(value));
			});
			return name.find(L"steamxr") != std::wstring::npos ||
				name.find(L"steamvr") != std::wstring::npos;
		}

		std::filesystem::path default_path_registry()
		{
			const auto local_app_data = read_environment(L"LOCALAPPDATA");
			if (!local_app_data.has_value() || local_app_data->empty()) return {};
			return std::filesystem::path(*local_app_data) / L"openvr" / L"openvrpaths.vrpath";
		}

		std::filesystem::path unique_temporary_directory()
		{
			std::error_code error;
			const auto root = std::filesystem::temp_directory_path(error);
			if (error) return {};
			return root / std::format("h2-mod-openvr-{}-{}", GetCurrentProcessId(), GetTickCount64());
		}

		std::string win32_message(const DWORD error)
		{
		wchar_t* buffer{};
		const auto length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
			reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
		if (length == 0 || buffer == nullptr) return std::format("Win32={}", error);
		std::wstring value(buffer, length);
		LocalFree(buffer);
		while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n')) value.pop_back();
		const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		if (required <= 0) return std::format("Win32={}", error);
		std::string result(static_cast<std::size_t>(required), '\0');
		(void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
			static_cast<int>(value.size()), result.data(), required, nullptr, nullptr);
		return result;
		}

		std::optional<std::wstring> token_user_sid(const HANDLE token)
		{
		DWORD required{};
		GetTokenInformation(token, TokenUser, nullptr, 0, &required);
		if (required == 0) return std::nullopt;
		std::vector<std::byte> buffer(required);
		if (!GetTokenInformation(token, TokenUser, buffer.data(), required, &required)) return std::nullopt;
		const auto user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
		LPWSTR sid_string{};
		if (!ConvertSidToStringSidW(user->User.Sid, &sid_string)) return std::nullopt;
		std::wstring result(sid_string);
		LocalFree(sid_string);
		return result;
		}

		std::optional<std::wstring> current_user_sid()
		{
		HANDLE token{};
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return std::nullopt;
		const auto result = token_user_sid(token);
		CloseHandle(token);
		return result;
		}

		std::optional<bool> token_elevated(const HANDLE token)
		{
			TOKEN_ELEVATION elevation{};
			DWORD returned{};
			if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned))
				return std::nullopt;
			return elevation.TokenIsElevated != FALSE;
		}

		std::optional<bool> process_elevated(const HANDLE process)
		{
			HANDLE token{};
			if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return std::nullopt;
			const auto result = token_elevated(token);
			CloseHandle(token);
			return result;
		}

		std::optional<DWORD> locate_vrserver()
		{
			const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
			PROCESSENTRY32W entry{sizeof(entry)};
			std::optional<DWORD> result;
			if (Process32FirstW(snapshot, &entry))
			{
				do
				{
					if (_wcsicmp(entry.szExeFile, L"vrserver.exe") == 0)
					{
						result = entry.th32ProcessID;
						break;
					}
				} while (Process32NextW(snapshot, &entry));
			}
			CloseHandle(snapshot);
			return result;
		}

		bool resolve_compositor_adapter(IVRSystem* const system,
			ID3D11Device* const game_device,
			Microsoft::WRL::ComPtr<IDXGIAdapter1>& runtime_adapter,
			std::int32_t& adapter_index, std::string& error)
		{
			runtime_adapter.Reset();
			adapter_index = -1;
			if (system == nullptr || game_device == nullptr)
			{
				error = "SteamVR adapter validation received no system or game D3D11 device";
				return false;
			}
			system->GetDXGIOutputInfo(&adapter_index);
			if (adapter_index < 0)
			{
				error = "SteamVR did not report a DXGI adapter";
				return false;
			}

			Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
				FAILED(factory->EnumAdapters1(static_cast<UINT>(adapter_index), &runtime_adapter)))
			{
				error = std::format("SteamVR DXGI adapter {} is unavailable through CreateDXGIFactory1",
					adapter_index);
				return false;
			}

			Microsoft::WRL::ComPtr<IDXGIDevice> game_dxgi_device;
			Microsoft::WRL::ComPtr<IDXGIAdapter> game_adapter;
			DXGI_ADAPTER_DESC game_description{};
			DXGI_ADAPTER_DESC1 runtime_description{};
			if (FAILED(game_device->QueryInterface(IID_PPV_ARGS(&game_dxgi_device))) ||
				FAILED(game_dxgi_device->GetAdapter(&game_adapter)) ||
				FAILED(game_adapter->GetDesc(&game_description)) ||
				FAILED(runtime_adapter->GetDesc1(&runtime_description)))
			{
				error = "could not compare the game and SteamVR DXGI adapters";
				return false;
			}
			if (game_description.AdapterLuid.HighPart != runtime_description.AdapterLuid.HighPart ||
				game_description.AdapterLuid.LowPart != runtime_description.AdapterLuid.LowPart)
			{
				error = std::format(
					"the game D3D11 device is not on SteamVR's DXGI adapter {}", adapter_index);
				return false;
			}
			error.clear();
			return true;
		}
	}

	path_registry_scope::path_registry_scope(std::string& diagnostic)
	{
		// Respect an explicit caller-selected registry. It may intentionally point
		// at a managed SteamVR installation or a test fixture.
		if (read_environment(L"VR_PATHREG_OVERRIDE").has_value()) return;

		const auto source = default_path_registry();
		std::error_code error;
		if (source.empty() || !std::filesystem::is_regular_file(source, error)) return;

		std::ifstream input(source, std::ios::binary);
		if (!input)
		{
			diagnostic = "OpenVR path registry exists but cannot be read: " + utf8_path(source);
			return;
		}
		const auto parsed = nlohmann::json::parse(input, nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object())
		{
			diagnostic = "OpenVR path registry is not valid JSON: " + utf8_path(source);
			return;
		}

		temporary_directory_ = unique_temporary_directory();
		if (temporary_directory_.empty() ||
			!std::filesystem::create_directories(temporary_directory_ / L"logs", error))
		{
			diagnostic = "could not create a process-local OpenVR log directory";
			return;
		}

		auto registry = parsed;
		registry["log"] = nlohmann::json::array({utf8_path(temporary_directory_ / L"logs")});
		registry_path_ = temporary_directory_ / L"openvrpaths.vrpath";
		{
			std::ofstream output(registry_path_, std::ios::binary | std::ios::trunc);
			if (!output)
			{
				diagnostic = "could not write the process-local OpenVR path registry";
				return;
			}
			output << registry.dump(2);
		}

		if (const auto previous = read_environment(L"VR_PATHREG_OVERRIDE"); previous.has_value())
		{
			had_previous_ = true;
			previous_value_ = *previous;
		}
		const auto override_value = registry_path_.wstring();
		if (!SetEnvironmentVariableW(L"VR_PATHREG_OVERRIDE", override_value.c_str()))
		{
			diagnostic = std::format("could not set VR_PATHREG_OVERRIDE (Win32={})", GetLastError());
			return;
		}
		active_ = true;
	}

	path_registry_scope::~path_registry_scope() noexcept
	{
		if (active_)
		{
			SetEnvironmentVariableW(L"VR_PATHREG_OVERRIDE",
				had_previous_ ? previous_value_.c_str() : nullptr);
		}
		std::error_code error;
		if (!temporary_directory_.empty())
		{
			std::filesystem::remove_all(temporary_directory_, error);
		}
	}

	std::string diagnose_ipc_environment()
	{
		// Do not open SteamVR_Namespace here. The namespace pipe is intentionally
		// single-instance and is often busy while vrmonitor/vrcompositor are using
		// it; probing it would create a false failure and perturb OpenVR startup.
		const auto server_pid = locate_vrserver();
		if (server_pid.has_value())
		{
			const auto server = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *server_pid);
			if (server == nullptr)
			{
				const auto error = GetLastError();
				if (error == ERROR_ACCESS_DENIED)
				{
					return std::format("SteamVR vrserver PID {} is running, but Windows denied "
						"process access. Launch H2-MOD VR under the same Windows user and elevation "
						"level as SteamVR (do not run one side as administrator).", *server_pid);
				}
				return std::format("SteamVR vrserver PID {} is unavailable: {}", *server_pid,
					win32_message(error));
			}
			DWORD server_session{};
			if (ProcessIdToSessionId(*server_pid, &server_session))
			{
				DWORD client_session{};
				if (ProcessIdToSessionId(GetCurrentProcessId(), &client_session) &&
					server_session != client_session)
				{
					CloseHandle(server);
					return std::format("SteamVR vrserver is in Windows session {}, but this Probe is in "
						"session {}. Run the Probe from the same interactive desktop session as SteamVR.",
						server_session, client_session);
				}
			}
			if (const auto server_elevated = process_elevated(server), client_elevated = process_elevated(GetCurrentProcess());
				server_elevated.has_value() && client_elevated.has_value() && *server_elevated != *client_elevated)
			{
				CloseHandle(server);
				return std::format("SteamVR vrserver and H2-MOD VR have different elevation levels "
					"(vrserver elevated={}, H2-MOD VR elevated={}). Run both at the same elevation level.",
					*server_elevated ? "true" : "false", *client_elevated ? "true" : "false");
			}
			HANDLE server_token{};
			if (OpenProcessToken(server, TOKEN_QUERY, &server_token))
			{
				const auto server_sid = token_user_sid(server_token);
				const auto client_sid = current_user_sid();
				CloseHandle(server_token);
				if (server_sid.has_value() && client_sid.has_value() && *server_sid != *client_sid)
				{
					CloseHandle(server);
					return "SteamVR vrserver is running under a different Windows user. Start H2-MOD VR "
						"from the interactive SteamVR user session so OpenVR can share its IPC resources.";
				}
			}
			CloseHandle(server);
			return {};
		}
		return "SteamVR vrserver is not running; start SteamVR and wait for vrserver";
	}

	std::string register_application()
	{
		try
		{
#ifdef DEBUG
			constexpr auto manifest_name = L"h2-mod-vr-debug.vrmanifest";
			constexpr auto app_key = "h2mod.vr.debug";
#else
			constexpr auto manifest_name = L"h2-mod-vr.vrmanifest";
			constexpr auto app_key = "h2mod.vr";
#endif
			std::array<wchar_t, 32768> executable{};
			const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
			if (length == 0 || length >= executable.size())
			{
				return "cannot resolve executable path for SteamVR application manifest";
			}
			const auto directory = std::filesystem::path(executable.data()).parent_path();
			const auto manifest = directory / manifest_name;
			// Resolve against the executable, never the caller's current directory.
			for (const auto& file : {manifest, directory / L"steamvr/cover.png", directory / L"vr_input/actions.json"})
			{
				std::error_code error;
				if (!std::filesystem::is_regular_file(file, error))
				{
					return "SteamVR application resource unavailable: " + utf8_path(file);
				}
			}
			auto* const applications = VRApplications();
			if (!applications) return "IVRApplications unavailable";
			// Submit the manifest on each initialization so artwork updates are read.
			// Keep registration across shutdowns for SteamVR library launch.
			const auto path = utf8_path(manifest);
			auto error = applications->AddApplicationManifest(path.c_str(), false);
			if (error != VRApplicationError_None)
			{
				return "AddApplicationManifest failed: " + std::to_string(error);
			}
			error = applications->IdentifyApplication(GetCurrentProcessId(), app_key);
			if (error != VRApplicationError_None)
			{
				return "IdentifyApplication failed: " + std::to_string(error);
			}
			return {};
		}
		catch (const std::exception& error)
		{
			return std::string("SteamVR application registration failed: ") + error.what();
		}
	}

	runtime_location locate_active_runtime()
	{
		runtime_location result;
		const auto manifest_path = read_active_manifest();
		if (!manifest_path.has_value())
		{
			result.error = "Windows has no active OpenXR runtime manifest";
			return result;
		}
		return inspect_runtime_manifest(*manifest_path);
	}

	runtime_location locate_installed_runtime()
	{
		std::array<char, 32768> path{};
		std::uint32_t required{};
		if (!VR_GetRuntimePath(path.data(), unsigned(path.size()), &required) || required <= 1 || required > path.size())
			return {.error = "OpenVR runtime installation path is unavailable"};
		const auto wide = widen_utf8(std::string_view(path.data(), required - 1));
		if (!wide)
			return {.error = "OpenVR runtime installation path is not valid UTF-8"};
		return inspect_runtime_manifest(std::filesystem::path(*wide) / L"steamxr_win64.json");
	}

	runtime_location inspect_runtime_manifest(const std::filesystem::path& manifest_path)
	{
		runtime_location result;
		result.manifest_path = utf8_path(manifest_path);
		result.steamvr_manifest = is_steamvr_manifest(manifest_path);
		std::error_code size_error;
		const auto manifest_size = std::filesystem::file_size(manifest_path, size_error);
		if (size_error || manifest_size > 1024 * 1024)
		{
			result.error = "the OpenXR runtime manifest is unavailable or exceeds 1 MiB";
			return result;
		}
		std::ifstream stream(manifest_path, std::ios::binary);
		if (!stream)
		{
			result.error = "the OpenXR runtime manifest cannot be opened";
			return result;
		}
		const auto manifest = nlohmann::json::parse(stream, nullptr, false);
		const auto runtime = manifest.is_object() ? manifest.find("runtime") : manifest.end();
		if (manifest.is_discarded() || runtime == manifest.end() || !runtime->is_object())
		{
			result.error = "the OpenXR runtime manifest is invalid";
			return result;
		}
		if (const auto name = runtime->find("name"); name != runtime->end() && name->is_string())
			result.runtime_name = name->get<std::string>();
		if (const auto marker = runtime->find("VALVE_runtime_is_steamvr");
		    marker != runtime->end() && marker->is_boolean())
			result.steamvr_manifest = marker->get<bool>();
		const auto library_value = runtime->find("library_path");
		if (library_value == runtime->end() || !library_value->is_string())
		{
			result.error = "the OpenXR runtime manifest has no runtime.library_path";
			return result;
		}
		const auto library_text = library_value->get<std::string>();
		if (library_text.empty() || library_text.find('\0') != std::string::npos)
		{
			result.error = "OpenXR runtime.library_path is empty or contains NUL";
			return result;
		}
		const auto wide_library = widen_utf8(library_text);
		if (!wide_library.has_value())
		{
			result.error = "OpenXR runtime.library_path is not valid UTF-8";
			return result;
		}
		auto library_path = std::filesystem::path(*wide_library);
		if (library_path.is_relative()) library_path = manifest_path.parent_path() / library_path;
		library_path = library_path.lexically_normal();
		result.client_library_path = utf8_path(library_path);
		std::error_code file_error;
		if (!std::filesystem::is_regular_file(library_path, file_error))
		{
			result.error = "the library declared by OpenXR runtime.library_path does not exist";
			return result;
		}
		if (!result.steamvr_manifest)
		{
			// Let the OpenXR loader negotiate the vendor ABI. Never load it as an
			// OpenVR client merely to inspect its name or installation path.
			result.valid = true;
			return result;
		}

		SetLastError(ERROR_SUCCESS);
		const auto module = LoadLibraryExW(library_path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
		const auto load_error = GetLastError();
		if (module == nullptr)
		{
			result.error = std::format("SteamVR runtime.library_path is not a loadable x64 DLL (Win32={})",
				load_error);
			return result;
		}
		const auto factory = GetProcAddress(module, "VRClientCoreFactory");
		FreeLibrary(module);
		if (factory == nullptr)
		{
			result.error = "SteamVR runtime.library_path does not export VRClientCoreFactory";
			return result;
		}
		result.valid = true;
		return result;
	}

	bool validate_compositor_adapter(IVRSystem* const system,
		ID3D11Device* const game_device, std::string& error)
	{
		if (game_device == nullptr)
		{
			error = "SteamVR direct submission received no H2 D3D11 device";
			return false;
		}
		// OpenVR explicitly requires CreateDXGIFactory1 (or later) before D3D11
		// device creation because the compositor must share the submitted texture
		// across its process boundary. Verify that the device actually retained
		// that factory lineage; a later Factory1 created only for LUID enumeration
		// cannot repair an already-created device.
		Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
		Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
		Microsoft::WRL::ComPtr<IDXGIFactory1> factory1;
		if (FAILED(game_device->QueryInterface(IID_PPV_ARGS(&dxgi_device))) ||
			FAILED(dxgi_device->GetAdapter(&adapter)) ||
			FAILED(adapter->GetParent(IID_PPV_ARGS(&factory1))))
		{
			error = "H2 D3D11 device was not created from a DXGI 1.1 factory; "
				"OpenVR direct Submit cannot share its textures";
			return false;
		}
		Microsoft::WRL::ComPtr<IDXGIAdapter1> runtime_adapter;
		std::int32_t adapter_index{-1};
		return resolve_compositor_adapter(system, game_device, runtime_adapter,
			adapter_index, error);
	}
}
