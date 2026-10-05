#pragma once
#include <string>
#include <filesystem>
#include <fstream>

namespace game_data
{
	inline constexpr std::uintmax_t supported_binary_size = 0xE1E0C8;
	inline std::string get_game_binary_path()
	{
		return std::ifstream("MW2CR.exe").good() ? "MW2CR.exe" : "h2_sp64_bnet_ship.exe";
	}

	// Match the loader's supported binary without loading the game or allocating
	// its full image. A missing profile alone never identifies an installation.
	inline bool is_game_directory_available() noexcept
	{
		try
		{
			const auto path = get_game_binary_path();
			if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) != supported_binary_size) return false;
			std::ifstream file(path, std::ios::binary);
			char signature[2]{};
			return file.read(signature, sizeof(signature)) && signature[0] == 'M' && signature[1] == 'Z';
		}
		catch (...) { return false; }
	}

	// Launcher reads the same profile that the game will initialize and use.
	std::string get_config_file_path();
	std::string get_config_source_path();
	void initialize_players_folder();
}
