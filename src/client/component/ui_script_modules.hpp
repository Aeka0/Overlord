#pragma once
#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ui_scripting::modules
{
	struct entry
	{
		std::string name;
		std::string initializer;
	};

	inline std::string identity(const std::filesystem::path& directory)
	{
		auto name = directory.filename().generic_string();
		// Module identifiers use Windows' case-insensitive ASCII path spelling.
		// Preserve non-ASCII bytes instead of applying a locale-dependent fold.
		for (auto& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		return name;
	}

	// The caller supplies the existing load order: cache/base first, active mod
	// and localized roots last. Resolve the complete overlay before executing
	// any Lua: shadowed initializers must never register callbacks or wrappers.
	inline std::vector<entry> discover(const std::vector<std::string>& roots_low_to_high)
	{
		std::vector<entry> candidates;
		std::unordered_map<std::string, std::size_t> winners;
		for (const auto& root : roots_low_to_high)
		{
			const auto folder = std::filesystem::path(root) / "ui_scripts";
			if (!std::filesystem::is_directory(folder)) continue;
			std::vector<std::filesystem::path> directories;
			for (const auto& child : std::filesystem::directory_iterator(folder))
				if (child.is_directory() && std::filesystem::is_regular_file(child.path() / "__init__.lua"))
					directories.push_back(child.path());
			std::sort(directories.begin(), directories.end());
			for (const auto& directory : directories)
			{
				auto name = identity(directory);
				winners[name] = candidates.size();
				candidates.push_back({std::move(name), (directory / "__init__.lua").generic_string()});
			}
		}
		std::vector<entry> selected;
		selected.reserve(winners.size());
		for (std::size_t index = 0; index < candidates.size(); ++index)
			if (winners.at(candidates[index].name) == index) selected.push_back(std::move(candidates[index]));
		return selected;
	}
}
