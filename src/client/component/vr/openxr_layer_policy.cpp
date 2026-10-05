#include <std_include.hpp>

#include "openxr_layer_policy.hpp"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <string_view>
#include <unordered_set>

#include <json.hpp>

namespace vr::openxr
{
	namespace
	{
		constexpr wchar_t implicit_layer_registry_path[] =
			L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
		constexpr std::string_view virtual_desktop_oculus_layer =
			"XR_APILAYER_VIRTUALDESKTOP_oculus_compatibility";
		constexpr wchar_t runtime_override_environment[] = L"XR_RUNTIME_JSON";

		bool is_high_integrity_process() noexcept
		{
			HANDLE token{};
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			{
				return true;
			}

			DWORD size{};
			(void)GetTokenInformation(token, TokenIntegrityLevel, nullptr, 0, &size);
			std::vector<std::byte> buffer(size);
			const auto queried = size != 0 && GetTokenInformation(token, TokenIntegrityLevel,
				buffer.data(), size, &size);
			CloseHandle(token);
			if (!queried)
			{
				return true;
			}

			const auto label = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(buffer.data());
			if (!IsValidSid(label->Label.Sid))
			{
				return true;
			}
			const auto count = *GetSidSubAuthorityCount(label->Label.Sid);
			if (count == 0)
			{
				return true;
			}
			const auto integrity = *GetSidSubAuthority(label->Label.Sid, count - 1);
			return integrity >= SECURITY_MANDATORY_HIGH_RID;
		}

		std::filesystem::path expand_environment_path(const std::wstring& path)
		{
			const auto required = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
			if (required == 0)
			{
				return path;
			}

			std::wstring expanded(required, L'\0');
			if (ExpandEnvironmentStringsW(path.c_str(), expanded.data(), required) == 0)
			{
				return path;
			}
			expanded.resize(required - 1);
			return expanded;
		}

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

		std::vector<std::filesystem::path> program_files_roots()
		{
			std::vector<std::filesystem::path> roots;
			for (const auto* variable : {L"ProgramW6432", L"ProgramFiles"})
			{
				if (const auto value = read_environment(variable); value.has_value())
				{
					roots.emplace_back(*value);
				}
			}
			std::unordered_set<std::wstring> seen;
			std::erase_if(roots, [&seen](const auto& candidate)
			{
				auto key = candidate.lexically_normal().wstring();
				std::ranges::transform(key, key.begin(), [](const wchar_t value)
				{
					return static_cast<wchar_t>(std::towlower(value));
				});
				return !seen.emplace(std::move(key)).second;
			});
			return roots;
		}

		void append_path_list(const std::wstring& path_list, std::vector<std::filesystem::path>& paths)
		{
			std::size_t start{};
			while (start < path_list.size())
			{
				const auto separator = path_list.find(L';', start);
				const auto length = separator == std::wstring::npos ? path_list.size() - start : separator - start;
				if (length != 0)
				{
					paths.push_back(expand_environment_path(path_list.substr(start, length)));
				}
				if (separator == std::wstring::npos) break;
				start = separator + 1;
			}
		}

		void append_registry_manifests(const HKEY hive, std::vector<std::filesystem::path>& paths,
			std::vector<std::string>& warnings)
		{
			HKEY key{};
			const auto opened = RegOpenKeyExW(hive, implicit_layer_registry_path, 0,
				KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
			if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND)
			{
				return;
			}
			if (opened != ERROR_SUCCESS)
			{
				warnings.push_back(std::format("cannot read implicit API layer registry (Win32={})", opened));
				return;
			}

			DWORD value_count{};
			DWORD maximum_name_length{};
			const auto queried = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
				&value_count, &maximum_name_length, nullptr, nullptr, nullptr);
			if (queried != ERROR_SUCCESS)
			{
				warnings.push_back(std::format("cannot inspect implicit API layer registry (Win32={})", queried));
				RegCloseKey(key);
				return;
			}

			std::vector<wchar_t> name(static_cast<std::size_t>(maximum_name_length) + 2);
			for (DWORD index = 0; index < value_count; ++index)
			{
				DWORD name_length = static_cast<DWORD>(name.size() - 1);
				DWORD enabled_value{1};
				DWORD value_size{sizeof(enabled_value)};
				const auto enumerated = RegEnumValueW(key, index, name.data(), &name_length, nullptr, nullptr,
					reinterpret_cast<BYTE*>(&enabled_value), &value_size);
				if (enumerated == ERROR_SUCCESS && value_size == sizeof(enabled_value) &&
					enabled_value == 0)
				{
					append_path_list(std::wstring{name.data(), name_length}, paths);
				}
			}
			RegCloseKey(key);
		}

		void append_default_virtual_desktop_manifest(std::vector<std::filesystem::path>& paths)
		{
			for (const auto& root : program_files_roots())
			{
				const auto manifest = root / L"Virtual Desktop Streamer" /
					L"openxr-oculus-compatibility.json";
				std::error_code error;
				if (std::filesystem::is_regular_file(manifest, error))
				{
					paths.push_back(manifest);
				}
			}
		}

		std::optional<std::filesystem::path> find_virtual_desktop_runtime_manifest()
		{
			for (const auto& root : program_files_roots())
			{
				const auto manifest = root / L"Virtual Desktop Streamer" / L"OpenXR" /
					L"virtualdesktop-openxr.json";
				std::error_code error;
				if (std::filesystem::is_regular_file(manifest, error)) return manifest;
			}
			return std::nullopt;
		}

		std::vector<std::filesystem::path> discover_implicit_layer_manifests(
			std::vector<std::string>& warnings)
		{
			std::vector<std::filesystem::path> paths;
			append_registry_manifests(HKEY_LOCAL_MACHINE, paths, warnings);
			if (!is_high_integrity_process())
			{
				append_registry_manifests(HKEY_CURRENT_USER, paths, warnings);
			}
			append_default_virtual_desktop_manifest(paths);

			std::unordered_set<std::wstring> seen;
			std::erase_if(paths, [&seen](const auto& path)
			{
				auto key = path.lexically_normal().wstring();
				std::ranges::transform(key, key.begin(), [](const wchar_t value)
				{
					return static_cast<wchar_t>(std::towlower(value));
				});
				return !seen.emplace(std::move(key)).second;
			});
			return paths;
		}

		bool valid_environment_name(const std::string_view value) noexcept
		{
			return !value.empty() && std::ranges::all_of(value, [](const unsigned char character)
			{
				return character >= 0x21 && character <= 0x7e && character != '=';
			});
		}

		std::wstring widen_ascii(const std::string_view value)
		{
			return {value.begin(), value.end()};
		}

		std::optional<std::wstring> widen_utf8(const std::string_view value)
		{
			if (value.empty()) return std::wstring{};
			const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), nullptr, 0);
			if (required <= 0) return std::nullopt;
			std::wstring wide(static_cast<std::size_t>(required), L'\0');
			if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
				static_cast<int>(value.size()), wide.data(), required) != required)
			{
				return std::nullopt;
			}
			return wide;
		}

		std::string path_for_diagnostic(const std::filesystem::path& path)
		{
			const auto& wide = path.native();
			const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
				static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
			if (required <= 0)
			{
				return "<unprintable path>";
			}
			std::string value(static_cast<std::size_t>(required), '\0');
			(void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
				static_cast<int>(wide.size()), value.data(), required, nullptr, nullptr);
			return value;
		}

		void process_manifest(const std::filesystem::path& path, implicit_layer_policy_result& result)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
			{
				result.warnings.push_back("cannot open implicit API layer manifest: " + path_for_diagnostic(path));
				return;
			}

			const auto manifest = nlohmann::json::parse(stream, nullptr, false);
			if (manifest.is_discarded() || !manifest.is_object())
			{
				result.warnings.push_back("invalid implicit API layer manifest: " + path_for_diagnostic(path));
				return;
			}

			const auto layer = manifest.find("api_layer");
			if (layer == manifest.end() || !layer->is_object())
			{
				return;
			}
			const auto name_value = layer->find("name");
			if (name_value == layer->end() || !name_value->is_string())
			{
				return;
			}
			const auto name = name_value->get<std::string>();
			if (name != virtual_desktop_oculus_layer)
			{
				return;
			}

			const auto disable_value = layer->find("disable_environment");
			const auto disable_environment = disable_value != layer->end() && disable_value->is_string()
				? disable_value->get<std::string>() : std::string{};
			if (!valid_environment_name(disable_environment))
			{
				result.blocking_error = "incompatible OpenXR layer has no valid disable_environment: " + name;
				return;
			}

			const auto environment_name = widen_ascii(disable_environment);
			if (!SetEnvironmentVariableW(environment_name.c_str(), L"1"))
			{
				result.blocking_error = std::format(
					"failed to disable incompatible OpenXR layer {} (Win32={})", name, GetLastError());
				return;
			}
			result.disabled_layers.push_back(name);
			result.disabled_environment_variables.push_back(disable_environment);
		}
	}

	implicit_layer_policy_result apply_native_implicit_layer_policy(
		const std::span<const std::filesystem::path> manifest_paths)
	{
		implicit_layer_policy_result result;
		result.manifest_count = manifest_paths.size();
		for (const auto& path : manifest_paths)
		{
			result.manifest_paths.push_back(path_for_diagnostic(path));
			process_manifest(path, result);
		}
		result.applied = result.blocking_error.empty();
		return result;
	}

	implicit_layer_policy_result apply_native_implicit_layer_policy()
	{
		implicit_layer_policy_result result;
		auto paths = discover_implicit_layer_manifests(result.warnings);
		auto applied = apply_native_implicit_layer_policy(paths);
		applied.warnings.insert(applied.warnings.begin(), result.warnings.begin(), result.warnings.end());
		return applied;
	}

	runtime_preference_result apply_runtime_preference(const std::filesystem::path& runtime_manifest)
	{
		runtime_preference_result result;
		result.applied = true;
		if (const auto existing = read_environment(runtime_override_environment); existing.has_value())
		{
			result.override_active = true;
			result.manifest_path = path_for_diagnostic(*existing);
			result.source = "existing_environment";
			return result;
		}

		std::ifstream stream(runtime_manifest, std::ios::binary);
		if (!stream)
		{
			result.warning = "preferred OpenXR runtime manifest is unavailable: " +
				path_for_diagnostic(runtime_manifest);
			return result;
		}
		const auto manifest = nlohmann::json::parse(stream, nullptr, false);
		const auto runtime = manifest.is_object() ? manifest.find("runtime") : manifest.end();
		if (manifest.is_discarded() || runtime == manifest.end() || !runtime->is_object())
		{
			result.warning = "preferred OpenXR runtime manifest is invalid: " +
				path_for_diagnostic(runtime_manifest);
			return result;
		}
		const auto library_value = runtime->find("library_path");
		if (library_value == runtime->end() || !library_value->is_string())
		{
			result.warning = "preferred OpenXR runtime manifest has no library_path: " +
				path_for_diagnostic(runtime_manifest);
			return result;
		}

		const auto library_text = library_value->get<std::string>();
		const auto library_wide = widen_utf8(library_text);
		if (!library_wide.has_value())
		{
			result.warning = "preferred OpenXR runtime library_path is not valid UTF-8";
			return result;
		}
		auto library = std::filesystem::path(*library_wide);
		if (library.is_relative()) library = runtime_manifest.parent_path() / library;
		std::error_code library_error;
		if (!std::filesystem::is_regular_file(library, library_error))
		{
			result.warning = "preferred OpenXR runtime library is unavailable: " + path_for_diagnostic(library);
			return result;
		}

		std::error_code absolute_error;
		const auto absolute_path = std::filesystem::absolute(runtime_manifest, absolute_error);
		if (absolute_error)
		{
			result.warning = "preferred OpenXR runtime manifest path cannot be resolved: " +
				path_for_diagnostic(runtime_manifest);
			return result;
		}
		const auto absolute_manifest = absolute_path.wstring();
		if (!SetEnvironmentVariableW(runtime_override_environment, absolute_manifest.c_str()))
		{
			result.blocking_error = std::format("failed to select preferred OpenXR runtime (Win32={})",
				GetLastError());
			return result;
		}
		result.override_active = true;
		result.override_set_by_policy = true;
		result.manifest_path = path_for_diagnostic(absolute_manifest);
		result.source = "automatic_virtual_desktop_vdxr";
		return result;
	}

	runtime_preference_result apply_virtual_desktop_runtime_preference()
	{
		if (const auto existing = read_environment(runtime_override_environment); existing.has_value())
		{
			return apply_runtime_preference({});
		}
		const auto manifest = find_virtual_desktop_runtime_manifest();
		if (!manifest.has_value())
		{
			runtime_preference_result result;
			result.applied = true;
			result.source = "system_active_runtime";
			return result;
		}
		return apply_runtime_preference(*manifest);
	}
}
