#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game_data.hpp"

#include "console.hpp"
#include "filesystem.hpp"
#include "mods.hpp"
#include "mod_stats.hpp"
#include "command.hpp"

#include <utils/hook.hpp>
#include <utils/io.hpp>
#include <utils/string.hpp>
#include <utils/concurrency.hpp>
#include <utils/thread.hpp>
#include <utils/properties.hpp>

#define PLAYERS_FOLDER "players2/"
#define DEFAULT_PLAYERS_FOLDER PLAYERS_FOLDER "default"
#define H2_MOD_PLAYERS_FOLDER PLAYERS_FOLDER "h2-mod"

namespace game_data
{
	namespace
	{
		std::optional<std::string> find_bnet_player_folder()
		{
			const auto dirs = utils::io::list_files(PLAYERS_FOLDER);
			for (const auto& dir : dirs)
			{
				if (dir == DEFAULT_PLAYERS_FOLDER || dir == H2_MOD_PLAYERS_FOLDER)
				{
					continue;
				}

				const auto cfg_file = std::format("{}\\config.cfg", dir);
				if (std::filesystem::exists(cfg_file))
				{
					return dir;
				}
			}

			return {};
		}

	}

	std::string get_config_file_path()
	{
		return H2_MOD_PLAYERS_FOLDER "/config.cfg";
	}

	std::string get_config_source_path()
	{
		if (std::filesystem::exists(get_config_file_path())) return get_config_file_path();
		if (std::filesystem::exists(DEFAULT_PLAYERS_FOLDER "/config.cfg")) return DEFAULT_PLAYERS_FOLDER "/config.cfg";
		const auto source = find_bnet_player_folder();
		return source ? *source + "/config.cfg" : get_config_file_path();
	}

	void initialize_players_folder()
	{
		if (!utils::io::directory_exists(PLAYERS_FOLDER)) return;
		const auto source = get_config_source_path();
		if (source == get_config_file_path()) return;
		// Use the same source as the launcher's read. A language preference can
		// create the destination first; retain it and any existing save files.
		std::filesystem::copy(std::filesystem::path(source).parent_path(), H2_MOD_PLAYERS_FOLDER,
			std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			initialize_players_folder();
			utils::hook::inject(0x14059D64E + 3, H2_MOD_PLAYERS_FOLDER);
		}
	};
}

REGISTER_COMPONENT(game_data::component)
