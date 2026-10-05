#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vr::openxr
{
	struct implicit_layer_policy_result
	{
		bool applied{};
		std::size_t manifest_count{};
		std::vector<std::string> manifest_paths;
		std::vector<std::string> disabled_layers;
		std::vector<std::string> disabled_environment_variables;
		std::vector<std::string> warnings;
		std::string blocking_error;
	};

	struct runtime_preference_result
	{
		bool applied{};
		bool override_active{};
		bool override_set_by_policy{};
		std::string manifest_path;
		std::string source;
		std::string warning;
		std::string blocking_error;
	};

	// Applies the native OpenXR policy before the loader is loaded. Compatibility
	// shims intended for a different engine integration must not wrap h2-mod's
	// direct OpenXR calls.
	[[nodiscard]] implicit_layer_policy_result apply_native_implicit_layer_policy();

	// Explicit-source overload used by smoke tests and diagnostic tools.
	[[nodiscard]] implicit_layer_policy_result apply_native_implicit_layer_policy(
		std::span<const std::filesystem::path> manifest_paths);

	// Prefers the native Virtual Desktop OpenXR runtime for this process only.
	// An existing XR_RUNTIME_JSON always wins.
	[[nodiscard]] runtime_preference_result apply_virtual_desktop_runtime_preference();
	[[nodiscard]] runtime_preference_result apply_runtime_preference(
		const std::filesystem::path& runtime_manifest);
}
